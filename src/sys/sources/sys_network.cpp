// 文件：src/sys/sys_network.cpp
/*
【模块职责】网络同步实现。

网络任务固定运行在 Core 0，主 UI 循环运行在 Arduino loop 所在核心。
主循环只负责状态检查和会话请求，真正的 WiFi/NTP/HTTP 与持久化 Outbox 消费全部在
network_daemon_task 中执行。

本文件的关键设计：
1. 开机不立刻启动 WiFi，而是通过 Network_RequestBootSync() 延迟触发，避免首屏卡顿；
2. 普通同步执行 NTP + API，周期校时只执行 NTP；
3. 网络任务只用 UDP NTP 获取 UTC epoch，不直接修改 ESP32 时钟；结果排队交给主循环统一应用并写入 RTC；
4. 失败后设置退避窗口，避免无网环境下反复打开 WiFi。
5. 任意成功联网会话都会消费 Outbox，具体业务通过 job_type 执行器注册，不写入网络编排代码。
*/
#include "sys/sys_network.h"
#include "sys/sys_config.h"
#include "sys/sys_time.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <cstring>
#include "sys/sys_router.h"
#include "sys/sys_event.h"
#include "sys/sys_constants.h"
#include "sys/sys_command_result.h"
#include "sys/sys_sleep_scheduler.h"
#include "sys/sys_network_outbox.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <queue>
#include <mutex>

volatile NetworkState g_state = NET_DISCONNECTED;
TaskHandle_t g_netTaskHandle = NULL;
static volatile bool g_worker_active = false;
static volatile bool g_abort_requested = false;
static volatile NetworkState g_abort_target_state = NET_SYNC_FAILED;

namespace
{
    bool NetworkStateBlocksSleep(NetworkState state)
    {
        return g_worker_active ||
               state == NET_CONNECTING ||
               state == NET_SYNCING_NTP ||
               state == NET_FETCHING_API ||
               state == NET_SYNC_SUCCESS;
    }

    /** 网络状态和休眠 blocker 必须同步更新，避免 Standby 复制一份状态判断规则。 */
    void NetworkSetState(NetworkState state)
    {
        g_state = state;
        SysSleep_SetBlocker(SysSleepBlocker::Network, NetworkStateBlocksSleep(state));
    }
}

/*
 * 主循环向 Core 0 投递不可变会话请求，避免 keep_alive/fetch_api 两个跨核全局变量被下一次
 * 请求改写。队列长度为 1：当前只允许一个会话运行，也不积压过时的“再次联网”请求。
 */
struct NetworkSessionRequest
{
    bool keep_alive;
    bool fetch_hidden_api;
};
static QueueHandle_t g_session_request_queue = nullptr;

/* 开机自动同步延迟触发状态：setup() 只登记，loop() 中到点触发。 */
static volatile bool g_boot_sync_pending = false;
static uint32_t g_boot_sync_due_ms = 0;

/* 网络总超时保险：防止底层 WiFi/NTP/HTTP 异常导致状态长时间卡住。 */
static uint32_t g_sync_started_ms = 0;
static constexpr uint32_t NETWORK_TOTAL_TIMEOUT_MS = 25000;

/* 周期轻量校时失败后的退避时间。无网时不要连续反复打开 WiFi。 */
static constexpr uint32_t TIME_RESYNC_RETRY_AFTER_FAIL_MS = 5UL * 60UL * 1000UL;
static uint32_t g_next_time_resync_allowed_ms = 0;

/* 每轮最多执行 8 项，防止待办积压后一次联网无限占用射频和电量。 */
static constexpr uint8_t NETWORK_OUTBOX_MAX_JOBS_PER_SESSION = 8;
static constexpr uint8_t NETWORK_JOB_HANDLER_CAPACITY = 12;
static constexpr uint32_t NETWORK_JOB_UNHANDLED_RETRY_SECONDS = 60UL * 60UL;

struct NetworkJobHandlerEntry
{
    uint16_t job_type = 0;
    NetworkJobHandler handler = nullptr;
};
static NetworkJobHandlerEntry g_job_handlers[NETWORK_JOB_HANDLER_CAPACITY];

/*
 * 不能使用 configTime()/configTzTime()：Arduino-ESP32 的 SNTP 客户端会在后台直接修改
 * 系统时钟，破坏“SysTime 是唯一写时钟入口”的所有权。这里发送最小 NTPv4 UDP 请求，
 * 网络任务只返回 UTC epoch，时区换算、settimeofday() 和 RTC 写回全部留给 SysTime。
 */
static constexpr uint16_t NTP_SERVER_PORT = 123;
static constexpr uint32_t NTP_RESPONSE_TIMEOUT_MS = 3200;
static constexpr uint64_t NTP_UNIX_EPOCH_OFFSET = 2208988800ULL;
static constexpr size_t NTP_PACKET_SIZE = 48;
static const char *const NTP_SERVERS[] = {
    "pool.ntp.org",
    "time.nist.gov",
    "ntp1.aliyun.com",
};

/*
 * V4B实测BLE与NFC完成后最大内部连续块约为7.6 KiB，旧10 KiB任务栈必然创建失败。
 * 本任务的大对象（String、HTTPClient、ArduinoJson文档）均由堆持有，函数内没有大尺寸数组；
 * 实机最坏的WiFi超时路径仍剩约4 KiB栈，因此固定使用7 KiB，不再保留每轮栈诊断日志。
 */
static constexpr uint32_t NETWORK_TASK_STACK_BYTES = 7U * 1024U;

namespace
{
    /** Core 0 网络任务只复制 API 结果；主循环再发布日程事件并保存配置。 */
    struct NetworkApiItem
    {
        uint32_t target_time = 0;
        String title;
        String text;
    };

    std::queue<NetworkApiItem> s_api_items;
    std::mutex s_api_items_mutex;
    constexpr size_t MAX_API_ITEMS = 5;

    void QueueApiItem(uint32_t target_time, const String &title, const String &text)
    {
        std::lock_guard<std::mutex> lock(s_api_items_mutex);
        while (s_api_items.size() >= MAX_API_ITEMS)
            s_api_items.pop();
        NetworkApiItem item;
        item.target_time = target_time;
        item.title = title;
        item.text = text;
        s_api_items.push(item);
    }

    bool PopApiItem(NetworkApiItem &out)
    {
        std::lock_guard<std::mutex> lock(s_api_items_mutex);
        if (s_api_items.empty())
            return false;
        out = s_api_items.front();
        s_api_items.pop();
        return true;
    }
}

/**
 * 判断网络任务是否正在占用 WiFi/NTP/API 流程。
 * 这些状态下不允许重复启动新的网络任务。
 */
bool Network_IsBusy()
{
    return NetworkStateBlocksSleep(g_state);
}

/**
 * BLE/网页下发 WIFI:ssid:pass 后的事件回调。
 *
 * 实现步骤：
 * 1. 校验 SSID 不能为空；
 * 2. 保存 SSID 和密码到公共配置；
 * 3. 回 ACK:OK:WIFI:SAVED；
 * 4. 立即启动一次完整同步，让设备拿时间并拉取隐秘指令。
 */
static void _Cb_WifiSet(void* payload)
{
    Evt_WifiSet_t* p = (Evt_WifiSet_t*)payload;

    if (!p || !p->ssid || String(p->ssid).length() == 0)
    {
        SysCmdResult_Error("EMPTY_SSID");
        return;
    }

    sysConfig.wifi_ssid = String(p->ssid);
    sysConfig.wifi_pass = String(p->pass);
    sysConfig.save();

    Serial.printf("[网络] WiFi 配置已保存，SSID=%s，开始完整同步。\n", p->ssid);
    SysCmdResult_Ok("SAVED", sysConfig.wifi_ssid);

    Network_StartSync(false);
}

/**
 * 连接 WiFi AP。
 *
 * 关键步骤：
 * - 关闭 persistent 和自动重连，避免失败后底层持续重试；
 * - disconnect 清理上一轮残留状态；
 * - WiFi.begin 后每 500ms 检查一次状态，最多等待约 10 秒。
 */
static bool _Network_ConnectWifi()
{
    NetworkSetState(NET_CONNECTING);
    Serial.println("[网络] 网络守护任务已唤醒，开始连接 WiFi...");

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    vTaskDelay(pdMS_TO_TICKS(120));

    WiFi.mode(WIFI_STA);
    WiFi.begin(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());

    int timeout_ticks = 0;
    while (!g_abort_requested && WiFi.status() != WL_CONNECTED && timeout_ticks < 20)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        timeout_ticks++;
    }

    return WiFi.status() == WL_CONNECTED;
}

/**
 * 执行 NTP 对时。
 *
 * 关键步骤：
 * - 逐个向三个服务器发送最小 NTPv4 UDP 请求，不启动会改系统时钟的 Arduino SNTP 服务；
 * - 校验响应来源、模式、层级和请求回显，拒绝旧包或无效服务器响应；
 * - 成功后只把 UTC epoch 投递给 SysTime，settimeofday、RTC 写入和状态更新都留在主循环。
 */
static bool _Network_SyncNtp(time_t &out_network_epoch)
{
    out_network_epoch = 0;
    NetworkSetState(NET_SYNCING_NTP);
    Serial.println("[网络] WiFi 已连接，开始 NTP 对时...");

    WiFiUDP udp;
    const uint16_t local_port = (uint16_t)(49152U + (esp_random() & 0x3FFFU));
    if (!udp.begin(local_port))
    {
        Serial.println("[网络] NTP UDP 本地端口创建失败。");
        return false;
    }

    for (const char *server_name : NTP_SERVERS)
    {
        if (g_abort_requested)
            break;

        IPAddress server_ip;
        if (WiFi.hostByName(server_name, server_ip) != 1)
        {
            Serial.printf("[网络] NTP 服务器域名解析失败：%s。\n", server_name);
            continue;
        }

        uint8_t request[NTP_PACKET_SIZE] = {};
        request[0] = 0x23; // LI=0、VN=4、Mode=3（客户端）。

        /*
         * 服务端会把客户端发送时间原样复制到 Originate Timestamp。ESP32 此时可能尚未校时，
         * 因此这里使用随机挑战值而不伪造时间；回包必须匹配，避免误收上一轮残留 UDP 包。
         */
        const uint32_t nonce_high = esp_random();
        const uint32_t nonce_low = esp_random();
        request[40] = (uint8_t)(nonce_high >> 24);
        request[41] = (uint8_t)(nonce_high >> 16);
        request[42] = (uint8_t)(nonce_high >> 8);
        request[43] = (uint8_t)nonce_high;
        request[44] = (uint8_t)(nonce_low >> 24);
        request[45] = (uint8_t)(nonce_low >> 16);
        request[46] = (uint8_t)(nonce_low >> 8);
        request[47] = (uint8_t)nonce_low;

        while (udp.parsePacket() > 0)
            udp.flush();

        if (!udp.beginPacket(server_ip, NTP_SERVER_PORT) ||
            udp.write(request, sizeof(request)) != sizeof(request) ||
            !udp.endPacket())
        {
            Serial.printf("[网络] NTP 请求发送失败：%s。\n", server_name);
            continue;
        }

        const uint32_t wait_started_ms = millis();
        while ((uint32_t)(millis() - wait_started_ms) < NTP_RESPONSE_TIMEOUT_MS)
        {
            if (g_abort_requested)
            {
                udp.stop();
                return false;
            }

            const int packet_size = udp.parsePacket();
            if (packet_size < (int)NTP_PACKET_SIZE)
            {
                if (packet_size > 0)
                    udp.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            uint8_t response[NTP_PACKET_SIZE] = {};
            const int read_length = udp.read(response, sizeof(response));
            const IPAddress response_ip = udp.remoteIP();
            const uint16_t response_port = udp.remotePort();
            udp.flush();
            if (read_length != (int)sizeof(response) ||
                response_ip != server_ip ||
                response_port != NTP_SERVER_PORT)
                continue;

            const uint8_t leap_indicator = response[0] >> 6;
            const uint8_t mode = response[0] & 0x07;
            const uint8_t stratum = response[1];
            if (leap_indicator == 3 || mode != 4 || stratum == 0 || stratum > 15 ||
                memcmp(&response[24], &request[40], 8) != 0)
                continue;

            const uint32_t seconds32 =
                ((uint32_t)response[40] << 24) |
                ((uint32_t)response[41] << 16) |
                ((uint32_t)response[42] << 8) |
                (uint32_t)response[43];

            /* NTP 32 位秒数在 2036 年回绕；小于 1900→1970 偏移时按 era 1 解释。 */
            uint64_t ntp_seconds = seconds32;
            if (ntp_seconds < NTP_UNIX_EPOCH_OFFSET)
                ntp_seconds += (1ULL << 32);
            const time_t network_epoch = (time_t)(ntp_seconds - NTP_UNIX_EPOCH_OFFSET);

            if (!SysTime_SubmitNetworkTime(network_epoch))
            {
                Serial.println("[网络] NTP 已返回，但 UTC 时间无法提交给主循环。");
                udp.stop();
                return false;
            }

            out_network_epoch = network_epoch;
            Serial.printf("[网络] 已收到 NTP 响应并交给主循环：服务器=%s。\n", server_name);
            udp.stop();
            return true;
        }

        Serial.printf("[网络] NTP 服务器响应超时：%s。\n", server_name);
    }

    udp.stop();
    return false;
}

/** 查找任务类型对应的执行器；注册表只在 setup 阶段写，网络任务运行期只读。 */
static NetworkJobHandler _Network_FindJobHandler(uint16_t job_type)
{
    for (const NetworkJobHandlerEntry &entry : g_job_handlers)
    {
        if (entry.job_type == job_type)
            return entry.handler;
    }
    return nullptr;
}

/** 默认指数退避从 30 秒起，最多 6 小时；服务端 Retry-After 可通过结果显式覆盖。 */
static uint32_t _Network_DefaultJobRetrySeconds(uint8_t attempt_count)
{
    const uint8_t exponent = attempt_count > 10 ? 10 : (attempt_count > 0 ? attempt_count - 1 : 0);
    uint32_t delay_seconds = 30UL << exponent;
    const uint32_t maximum_seconds = 6UL * 60UL * 60UL;
    return delay_seconds > maximum_seconds ? maximum_seconds : delay_seconds;
}

/**
 * 在已联网且 NTP 已返回后消费持久化 Outbox。
 * 领取动作先落盘 attempt_count，再调用业务执行器；因此执行中掉电只会造成安全重放。
 */
static void _Network_DrainOutbox(time_t network_epoch)
{
    const SysNetworkOutboxStats initial_stats = SysNetworkOutbox_GetStats();
    if (!initial_stats.storage_ready || initial_stats.pending_count == 0)
        return;

    NetworkSetState(NET_FETCHING_API);
    Serial.printf("[网络] 开始处理联网待办：本轮上限=%u，当前待处理=%u。\n",
                  static_cast<unsigned>(NETWORK_OUTBOX_MAX_JOBS_PER_SESSION),
                  static_cast<unsigned>(initial_stats.pending_count));

    for (uint8_t index = 0; index < NETWORK_OUTBOX_MAX_JOBS_PER_SESSION; ++index)
    {
        if (g_abort_requested)
            break;

        SysNetworkJob job;
        if (!SysNetworkOutbox_ClaimNextReady(static_cast<int64_t>(network_epoch), &job))
            break;

        NetworkJobHandler handler = _Network_FindJobHandler(job.job_type);
        if (!handler)
        {
            const int64_t retry_epoch = static_cast<int64_t>(network_epoch) + NETWORK_JOB_UNHANDLED_RETRY_SECONDS;
            SysNetworkOutbox_Retry(job.job_id, retry_epoch);
            Serial.printf("[网络] 任务类型 %u 尚未注册执行器，保留任务并延后 1 小时。\n",
                          static_cast<unsigned>(job.job_type));
            continue;
        }

        Serial.printf("[网络] 执行待办：类型=%u，尝试=%u，ID=%08lX%08lX。\n",
                      static_cast<unsigned>(job.job_type),
                      static_cast<unsigned>(job.attempt_count),
                      static_cast<unsigned long>(job.job_id >> 32),
                      static_cast<unsigned long>(job.job_id));

        const NetworkJobExecutionResult result = handler(job, static_cast<int64_t>(network_epoch));
        bool saved = false;
        if (result.disposition == NetworkJobDisposition::Complete)
        {
            saved = SysNetworkOutbox_Complete(job.job_id);
            Serial.printf("[网络] 联网待办%s确认完成。\n", saved ? "已" : "未能");
        }
        else if (result.disposition == NetworkJobDisposition::PermanentFailure)
        {
            saved = SysNetworkOutbox_DeadLetter(job.job_id);
            Serial.printf("[网络] 联网待办发生永久错误，%s转入死信。\n", saved ? "已" : "未能");
        }
        else
        {
            const uint32_t delay_seconds = result.retry_after_seconds > 0
                                               ? result.retry_after_seconds
                                               : _Network_DefaultJobRetrySeconds(job.attempt_count);
            saved = SysNetworkOutbox_Retry(job.job_id, static_cast<int64_t>(network_epoch) + delay_seconds);
            Serial.printf("[网络] 联网待办暂时失败，%s设置 %lu 秒后重试。\n",
                          saved ? "已" : "未能",
                          static_cast<unsigned long>(delay_seconds));
        }

        /* 状态提交失败时停止本轮，避免同一任务在一次会话内紧密重复。 */
        if (!saved)
            break;
    }
}

/**
 * 拉取云端隐秘指令 API。
 *
 * 该步骤只在完整同步中执行。
 * 周期轻量校时不会调用它，避免周期性重复请求服务器。
 *
 * API 返回数组后只复制到跨核心队列；Network_Update() 在主循环复用隐藏日程写入链路。
 */
static void _Network_FetchHiddenPrescripts()
{
    NetworkSetState(NET_FETCHING_API);
    Serial.println("[网络] NTP 对时完成，开始请求隐秘指令 API...");

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(4000);

    if (!http.begin(client, PrescriptConst::NETWORK_SYNC_URL))
    {
        Serial.println("[网络] HTTP 初始化失败。");
        return;
    }

    int httpCode = http.GET();

    if (httpCode == 200)
    {
        String payload = http.getString();
        JsonDocument doc;

        if (!deserializeJson(doc, payload))
        {
            JsonArray arr = doc.as<JsonArray>();
            int count = 0;

            for (JsonObject obj : arr)
            {
                if (count >= 5)
                    break;

                uint32_t tt = obj["time"] | 0;
                String tl = obj["title"] | "隐秘行动";
                String ps = obj["ps"] | "";

                if (tt > 0)
                {
                    QueueApiItem(tt, tl, ps);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }

                count++;
            }

            Serial.printf("[网络] 已导入 %d 条隐秘指令日程。\n", count);
        }
        else
        {
            Serial.println("[网络] API 返回内容解析失败。");
        }
    }
    else
    {
        Serial.printf("[网络] API 请求失败，HTTP 状态码=%d。\n", httpCode);
    }

    http.end();
}

/**
 * 网络失败后的统一收尾。
 *
 * 参数 state 用来区分 WiFi 连接失败和 NTP/API 同步失败。
 * 失败后设置 5 分钟退避窗口，避免无网环境下周期校时频繁唤醒 WiFi。
 */
/**
 * 【函数说明】判断是否配置了真实 WiFi。
 * 【关键原因】默认配置/急救默认使用占位符 SSID，若按“非空即连接”处理，
 * 每次开机都会尝试连接不存在的 AP，WiFi 驱动会占用约 60 KiB 内部堆，
 * 且 Arduino 的 WiFi.mode(WIFI_OFF) 不保证完全回收，导致后续 LittleFS
 * （待机图、配置保存）因内存不足而失败。
 */
static bool isWifiConfigured()
{
    return !sysConfig.wifi_ssid.isEmpty() &&
           sysConfig.wifi_ssid != "Your_WiFi_Name";
}

static void _Network_FailAndShutdown(NetworkState state)
{
    g_sync_started_ms = 0;
    g_next_time_resync_allowed_ms = millis() + TIME_RESYNC_RETRY_AFTER_FAIL_MS;

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    g_worker_active = false;
    NetworkSetState(state);
}

/**
 * Core 0 后台网络守护任务。
 *
 * 执行流程：
 * 1. 等待 Network_StartSync / Network_StartTimeSyncOnly 投递不可变会话请求；
 * 2. 从请求中读取本轮是否 fetch API、是否保持 WiFi；
 * 3. 连接 WiFi；
 * 4. NTP 对时；
 * 5. 消费持久化联网待办；
 * 6. 根据本轮模式决定是否请求隐秘指令 API；
 * 7. 成功后更新下一次允许校时时间；
 * 8. 根据 keep_alive 决定保持在线或关闭 WiFi。
 */
static void network_daemon_task(void *pvParameters)
{
    while (true)
    {
        NetworkSessionRequest request = {};
        if (!g_session_request_queue ||
            xQueueReceive(g_session_request_queue, &request, portMAX_DELAY) != pdTRUE)
            continue;

        if (!isWifiConfigured())
        {
            Serial.println("[网络] 未配置真实 WiFi，跳过自动同步。");
            g_next_time_resync_allowed_ms = millis() + TIME_RESYNC_RETRY_AFTER_FAIL_MS;
            g_sync_started_ms = 0;
            g_worker_active = false;
            NetworkSetState(NET_DISCONNECTED);
            continue;
        }

        if (!_Network_ConnectWifi())
        {
            Serial.println("[网络] WiFi 连接超时。");
            _Network_FailAndShutdown(g_abort_requested ? g_abort_target_state : NET_CONNECT_FAILED);
            continue;
        }

        time_t network_epoch = 0;
        if (!_Network_SyncNtp(network_epoch))
        {
            Serial.println("[网络] NTP 对时超时。");
            _Network_FailAndShutdown(g_abort_requested ? g_abort_target_state : NET_SYNC_FAILED);
            continue;
        }

        if (g_abort_requested)
        {
            _Network_FailAndShutdown(g_abort_target_state);
            continue;
        }

        /* 无论本轮因何联网，只要连接和校时成功，就顺手处理“下次联网”待办。 */
        _Network_DrainOutbox(network_epoch);

        if (g_abort_requested)
        {
            _Network_FailAndShutdown(g_abort_target_state);
            continue;
        }

        if (request.fetch_hidden_api)
        {
            _Network_FetchHiddenPrescripts();
        }
        else
        {
            Serial.println("[网络] 轻量 NTP 校时完成，本轮跳过 API。");
        }

        NetworkSetState(NET_SYNC_SUCCESS);
        g_sync_started_ms = 0;

        /*
         * 下一次周期校时至少等用户设置的间隔。
         * 如果用户设置了 5/15/30/60 分钟，Network_Update 会按这个值重新判断。
         */
        g_next_time_resync_allowed_ms = millis() + ((uint32_t)sysConfig.time_resync_interval_min * 60UL * 1000UL);

        Serial.println("[网络] 本轮网络任务完成。");
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (g_abort_requested)
        {
            _Network_FailAndShutdown(g_abort_target_state);
        }
        else if (!request.keep_alive)
        {
            Serial.println("[网络] 自动同步结束，正在关闭 WiFi。");
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);
            g_worker_active = false;
            NetworkSetState(NET_DISCONNECTED);
        }
        else
        {
            g_worker_active = false;
            NetworkSetState(NET_SYNC_SUCCESS);
            Serial.println("[网络] 手动连接模式，保持 WiFi 在线。");
        }
    }
}

void Network_Init()
{
    NetworkSetState(NET_DISCONNECTED);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    /* 周期校时初始窗口：启动后至少等一个用户配置间隔，不会和开机自动同步抢。 */
    g_next_time_resync_allowed_ms = millis() + ((uint32_t)sysConfig.time_resync_interval_min * 60UL * 1000UL);

    SysEvent_Subscribe(EVT_WIFI_SET, _Cb_WifiSet);

    g_session_request_queue = xQueueCreate(1, sizeof(NetworkSessionRequest));
    if (!g_session_request_queue)
    {
        Serial.println("[网络] 无法创建联网会话请求队列。");
        return;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        network_daemon_task,
        "NetDaemon",
        NETWORK_TASK_STACK_BYTES,
        NULL,
        1,
        &g_netTaskHandle,
        0
    );
    if (created != pdPASS)
    {
        g_netTaskHandle = NULL;
        Serial.printf("[网络] 后台任务创建失败；内部堆=%u字节，最大内部块=%u字节。\n",
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
}

bool Network_RegisterJobHandler(uint16_t job_type, NetworkJobHandler handler)
{
    if (job_type == 0 || !handler || g_worker_active)
        return false;

    for (NetworkJobHandlerEntry &entry : g_job_handlers)
    {
        if (entry.job_type == job_type)
            return false;
        if (entry.job_type == 0)
        {
            entry.job_type = job_type;
            entry.handler = handler;
            Serial.printf("[网络] 已注册联网待办执行器：类型=%u。\n", static_cast<unsigned>(job_type));
            return true;
        }
    }

    Serial.println("[网络] 联网待办执行器注册表已满。");
    return false;
}

static bool _Network_QueueSession(const NetworkSessionRequest &request, const char *description)
{
    if (!g_netTaskHandle || !g_session_request_queue)
    {
        Serial.printf("[网络] %s被忽略：网络任务尚未创建。\n", description);
        return false;
    }
    if (!isWifiConfigured())
    {
        Serial.printf("[网络] %s被忽略：尚未配置真实 WiFi。\n", description);
        NetworkSetState(NET_CONNECT_FAILED);
        return false;
    }
    if (Network_IsBusy())
    {
        Serial.printf("[网络] %s被忽略：网络会话正在运行或保持在线。\n", description);
        return false;
    }

    g_abort_requested = false;
    g_abort_target_state = NET_SYNC_FAILED;
    g_sync_started_ms = millis();
    g_worker_active = true;
    NetworkSetState(NET_CONNECTING);
    if (xQueueSend(g_session_request_queue, &request, 0) != pdTRUE)
    {
        g_worker_active = false;
        g_sync_started_ms = 0;
        NetworkSetState(NET_SYNC_FAILED);
        Serial.printf("[网络] %s入队失败。\n", description);
        return false;
    }
    return true;
}

void Network_StartSync(bool keep_alive)
{
    /*
     * 如果用户在开机自动同步延迟期间手动触发网络同步，
     * 取消原定的开机同步，避免几秒后重复启动第二轮同步。
     */
    g_boot_sync_pending = false;

    const NetworkSessionRequest request = {keep_alive, true};
    _Network_QueueSession(request, "完整同步请求");
}

void Network_StartTimeSyncOnly()
{
    Serial.println("[网络] 开始轻量 NTP 校时。");
    const NetworkSessionRequest request = {false, false};
    _Network_QueueSession(request, "轻量校时请求");
}

NetworkState Network_GetState()
{
    return g_state;
}

void Network_RequestBootSync(uint32_t delay_ms)
{
    if (sysConfig.wifi_ssid.isEmpty())
    {
        Serial.println("[网络] 未配置 WiFi，跳过开机自动同步。");
        return;
    }

    g_boot_sync_pending = true;
    g_boot_sync_due_ms = millis() + delay_ms;

    Serial.printf("[网络] 开机自动同步将在 %lu ms 后触发。\n", (unsigned long)delay_ms);
}

void Network_Update()
{
    uint32_t now = millis();

    /* 网络 API 结果在主循环发布，确保日程数组、LittleFS 和业务 ACK 不在 Core 0 修改。 */
    NetworkApiItem api_item;
    while (PopApiItem(api_item))
        SysRouter_ProcessAPI(api_item.target_time, api_item.title, api_item.text);

    /* 1. 到点触发开机完整同步：NTP + API。 */
    if (g_boot_sync_pending && (int32_t)(now - g_boot_sync_due_ms) >= 0)
    {
        g_boot_sync_pending = false;
        Serial.println("[网络] 触发延迟开机完整同步。");
        Network_StartSync(false);
    }

    /* 2. 网络总超时兜底，防止状态长时间停在连接/NTP/API 阶段。 */
    if (Network_IsBusy() &&
        g_sync_started_ms > 0 &&
        now - g_sync_started_ms > NETWORK_TOTAL_TIMEOUT_MS)
    {
        Serial.println("[网络] 网络任务总超时，强制中止本轮同步。");
        Network_Abort();
    }

    /*
     * 3. 周期轻量校时。
     *
     * 条件全部满足才启动：
     * - 用户在时间设置里开启了周期校时；
     * - 已配置 WiFi；
     * - 当前没有等待中的开机同步；
     * - 网络任务不忙；
     * - 退避/间隔窗口已到；
     * - SysTime 判断已经超过校时间隔或从未 NTP 对时。
     */
    if (sysConfig.time_auto_resync &&
        !sysConfig.wifi_ssid.isEmpty() &&
        !g_boot_sync_pending &&
        !Network_IsBusy())
    {
        uint32_t interval_ms = (uint32_t)sysConfig.time_resync_interval_min * 60UL * 1000UL;
        bool window_due = (int32_t)(now - g_next_time_resync_allowed_ms) >= 0;
        bool clock_due = SysTime_ShouldPeriodicResync(interval_ms);

        if (window_due && clock_due)
        {
            Serial.println("[网络] 已达到周期校时间隔，开始轻量 NTP 校时。");

            /*
             * 先推后一次允许时间。
             * 如果本轮失败，失败收尾会设置 5 分钟后重试；
             * 如果本轮成功，成功收尾会设置为用户配置的间隔。
             */
            g_next_time_resync_allowed_ms = now + TIME_RESYNC_RETRY_AFTER_FAIL_MS;
            Network_StartTimeSyncOnly();
        }
    }
}

void Network_Abort()
{
    g_boot_sync_pending = false;
    g_sync_started_ms = 0;
    g_next_time_resync_allowed_ms = millis() + TIME_RESYNC_RETRY_AFTER_FAIL_MS;
    g_abort_target_state = NET_SYNC_FAILED;
    g_abort_requested = true;

    if (g_session_request_queue)
        xQueueReset(g_session_request_queue);

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    NetworkSetState(NET_SYNC_FAILED);
    Serial.println("[网络] 已强制关闭 WiFi，状态置为 NET_SYNC_FAILED。");
}

void Network_Disconnect()
{
    g_boot_sync_pending = false;
    g_sync_started_ms = 0;
    g_abort_target_state = NET_DISCONNECTED;
    g_abort_requested = true;

    if (g_session_request_queue)
        xQueueReset(g_session_request_queue);

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    NetworkSetState(NET_DISCONNECTED);
    Serial.println("[网络] 已按用户请求关闭 WiFi。");
}
