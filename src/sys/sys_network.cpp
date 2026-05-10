// 文件：src/sys/sys_network.cpp
/*
【模块职责】网络同步实现。

网络任务固定运行在 Core 0，主 UI 循环运行在 Arduino loop 所在核心。
主循环只负责状态检查和任务唤醒，真正的 WiFi/NTP/HTTP 全部在 network_daemon_task 中执行。

本文件的关键设计：
1. 开机不立刻启动 WiFi，而是通过 Network_RequestBootSync() 延迟触发，避免首屏卡顿；
2. 普通同步执行 NTP + API，周期校时只执行 NTP；
3. 每次 NTP 成功后通知 SysTime_MarkNetworkSynced()，作为周期校时的时间基准；
4. 失败后设置退避窗口，避免无网环境下反复打开 WiFi。
*/
#include "sys_network.h"
#include "sys_config.h"
#include "sys_time.h"
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "sys_router.h"
#include "sys_event.h"
#include "sys_constants.h"
#include "sys_command_result.h"

volatile NetworkState g_state = NET_DISCONNECTED;
TaskHandle_t g_netTaskHandle = NULL;

/* keep_alive=true 时，完整同步完成后保持 WiFi 在线；否则任务收尾时关闭 WiFi。 */
static volatile bool g_keep_wifi_alive = false;

/*
 * 本轮网络任务是否请求隐秘指令 API。
 * - Network_StartSync() 设置为 true：完整同步；
 * - Network_StartTimeSyncOnly() 设置为 false：周期轻量校时。
 */
static volatile bool g_fetch_api_this_round = true;

/* 开机自动同步延迟触发状态：setup() 只登记，loop() 中到点触发。 */
static volatile bool g_boot_sync_pending = false;
static uint32_t g_boot_sync_due_ms = 0;

/* 网络总超时保险：防止底层 WiFi/NTP/HTTP 异常导致状态长时间卡住。 */
static uint32_t g_sync_started_ms = 0;
static constexpr uint32_t NETWORK_TOTAL_TIMEOUT_MS = 25000;

/* 周期轻量校时失败后的退避时间。无网时不要连续反复打开 WiFi。 */
static constexpr uint32_t TIME_RESYNC_RETRY_AFTER_FAIL_MS = 5UL * 60UL * 1000UL;
static uint32_t g_next_time_resync_allowed_ms = 0;

/**
 * 判断网络任务是否正在占用 WiFi/NTP/API 流程。
 * 这些状态下不允许重复启动新的网络任务。
 */
static bool _Network_IsBusy()
{
    return g_state == NET_CONNECTING ||
           g_state == NET_SYNCING_NTP ||
           g_state == NET_FETCHING_API;
}

/**
 * BLE/网页下发 WIFI:ssid:pass 后的事件回调。
 *
 * 实现步骤：
 * 1. 校验 SSID 不能为空；
 * 2. 保存 SSID 和密码到 config.json；
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
    g_state = NET_CONNECTING;
    Serial.println("[网络] 网络守护任务已唤醒，开始连接 WiFi...");

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    vTaskDelay(pdMS_TO_TICKS(120));

    WiFi.mode(WIFI_STA);
    WiFi.begin(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());

    int timeout_ticks = 0;
    while (WiFi.status() != WL_CONNECTED && timeout_ticks < 20)
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
 * - 调用 configTime 配置 NTP 服务器；
 * - 使用 getLocalTime(&tm, 0) 非阻塞轮询；
 * - 每 500ms 检查一次，最多约 10 秒；
 * - 成功后调用 SysTime_MarkNetworkSynced() 记录本次对时时刻。
 */
static bool _Network_SyncNtp()
{
    g_state = NET_SYNCING_NTP;
    Serial.println("[网络] WiFi 已连接，开始 NTP 对时...");

    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov", "ntp1.aliyun.com");

    int timeout_ticks = 0;
    struct tm timeinfo;

    while (!getLocalTime(&timeinfo, 0) && timeout_ticks < 20)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        timeout_ticks++;
    }

    if (timeout_ticks >= 20)
        return false;

    SysTime_MarkNetworkSynced();
    return true;
}

/**
 * 拉取云端隐秘指令 API。
 *
 * 该步骤只在完整同步中执行。
 * 周期轻量校时不会调用它，避免周期性重复请求服务器。
 *
 * API 返回数组后，每条有效记录转给 SysRouter_ProcessAPI()，复用原有隐藏日程写入链路。
 */
static void _Network_FetchHiddenPrescripts()
{
    g_state = NET_FETCHING_API;
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
                    SysRouter_ProcessAPI(tt, tl, ps);
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
static void _Network_FailAndShutdown(NetworkState state)
{
    g_state = state;
    g_sync_started_ms = 0;
    g_next_time_resync_allowed_ms = millis() + TIME_RESYNC_RETRY_AFTER_FAIL_MS;

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}

/**
 * Core 0 后台网络守护任务。
 *
 * 执行流程：
 * 1. 等待 Network_StartSync / Network_StartTimeSyncOnly 发送通知；
 * 2. 锁定本轮是否 fetch API；
 * 3. 连接 WiFi；
 * 4. NTP 对时；
 * 5. 根据本轮模式决定是否请求隐秘指令 API；
 * 6. 成功后更新下一次允许校时时间；
 * 7. 根据 keep_alive 决定保持在线或关闭 WiFi。
 */
static void network_daemon_task(void *pvParameters)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        bool fetch_api = g_fetch_api_this_round;

        if (sysConfig.wifi_ssid.isEmpty())
        {
            _Network_FailAndShutdown(NET_CONNECT_FAILED);
            continue;
        }

        if (!_Network_ConnectWifi())
        {
            Serial.println("[网络] WiFi 连接超时。");
            _Network_FailAndShutdown(NET_CONNECT_FAILED);
            continue;
        }

        if (!_Network_SyncNtp())
        {
            Serial.println("[网络] NTP 对时超时。");
            _Network_FailAndShutdown(NET_SYNC_FAILED);
            continue;
        }

        if (fetch_api)
        {
            _Network_FetchHiddenPrescripts();
        }
        else
        {
            Serial.println("[网络] 轻量 NTP 校时完成，本轮跳过 API。");
        }

        g_state = NET_SYNC_SUCCESS;
        g_sync_started_ms = 0;

        /*
         * 下一次周期校时至少等用户设置的间隔。
         * 如果用户设置了 5/15/30/60 分钟，Network_Update 会按这个值重新判断。
         */
        g_next_time_resync_allowed_ms = millis() + ((uint32_t)sysConfig.time_resync_interval_min * 60UL * 1000UL);

        Serial.println("[网络] 本轮网络任务完成。");
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (!g_keep_wifi_alive)
        {
            Serial.println("[网络] 自动同步结束，正在关闭 WiFi。");
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);
            g_state = NET_DISCONNECTED;
        }
        else
        {
            Serial.println("[网络] 手动连接模式，保持 WiFi 在线。");
        }
    }
}

void Network_Init()
{
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    /* 周期校时初始窗口：启动后至少等一个用户配置间隔，不会和开机自动同步抢。 */
    g_next_time_resync_allowed_ms = millis() + ((uint32_t)sysConfig.time_resync_interval_min * 60UL * 1000UL);

    SysEvent_Subscribe(EVT_WIFI_SET, _Cb_WifiSet);

    xTaskCreatePinnedToCore(
        network_daemon_task,
        "NetDaemon",
        10240,
        NULL,
        1,
        &g_netTaskHandle,
        0
    );
}

void Network_StartSync(bool keep_alive)
{
    g_keep_wifi_alive = keep_alive;
    g_fetch_api_this_round = true;

    /*
     * 如果用户在开机自动同步延迟期间手动触发网络同步，
     * 取消原定的开机同步，避免几秒后重复启动第二轮同步。
     */
    g_boot_sync_pending = false;

    if (g_netTaskHandle == NULL)
    {
        Serial.println("[网络] 同步请求被忽略：网络任务尚未创建。");
        return;
    }

    if (sysConfig.wifi_ssid.isEmpty())
    {
        Serial.println("[网络] 同步请求被忽略：SSID 为空。");
        g_state = NET_CONNECT_FAILED;
        return;
    }

    if (_Network_IsBusy())
    {
        Serial.println("[网络] 同步请求被忽略：网络任务正在运行。");
        return;
    }

    g_sync_started_ms = millis();
    g_state = NET_CONNECTING;
    xTaskNotifyGive(g_netTaskHandle);
}

void Network_StartTimeSyncOnly()
{
    g_keep_wifi_alive = false;
    g_fetch_api_this_round = false;

    if (g_netTaskHandle == NULL)
    {
        Serial.println("[网络] 轻量校时被忽略：网络任务尚未创建。");
        return;
    }

    if (sysConfig.wifi_ssid.isEmpty())
    {
        Serial.println("[网络] 轻量校时被忽略：SSID 为空。");
        return;
    }

    if (_Network_IsBusy())
    {
        Serial.println("[网络] 轻量校时被忽略：网络任务正在运行。");
        return;
    }

    g_sync_started_ms = millis();
    g_state = NET_CONNECTING;

    Serial.println("[网络] 开始轻量 NTP 校时。");
    xTaskNotifyGive(g_netTaskHandle);
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

    /* 1. 到点触发开机完整同步：NTP + API。 */
    if (g_boot_sync_pending && (int32_t)(now - g_boot_sync_due_ms) >= 0)
    {
        g_boot_sync_pending = false;
        Serial.println("[网络] 触发延迟开机完整同步。");
        Network_StartSync(false);
    }

    /* 2. 网络总超时兜底，防止状态长时间停在连接/NTP/API 阶段。 */
    if (_Network_IsBusy() &&
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
        !_Network_IsBusy())
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
    g_keep_wifi_alive = false;
    g_boot_sync_pending = false;
    g_sync_started_ms = 0;
    g_next_time_resync_allowed_ms = millis() + TIME_RESYNC_RETRY_AFTER_FAIL_MS;

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    g_state = NET_SYNC_FAILED;
    Serial.println("[网络] 已强制关闭 WiFi，状态置为 NET_SYNC_FAILED。");
}
