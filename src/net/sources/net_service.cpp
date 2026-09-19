// 文件：src/net/sources/net_service.cpp
/*
【模块职责】顶层网络会话状态机。

【所有权边界】
- NetService：会话策略、WiFi 生命周期和状态；
- NetNtp：每次会话必经的可信时间基础设施；
- NetTaskRegistry：普通业务任务执行与主循环结果泵送；
- NetOutbox：持久任务存储，不感知 WiFi 和业务协议。

网络守护固定在 Core 0。UI、LittleFS 业务对象和 I2C 仍只在 Arduino 主循环修改。
*/
#include "net/net_service.h"
#include "net/net_tls_memory.h"

#include <WiFi.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "net/net_ntp.h"
#include "net/net_auth_session.h"
#include "net/net_outbox.h"
#include "net/net_task_registry.h"
#include "net/net_builtin_tasks.h"
#include "sys/sys_command_result.h"
#include "sys/sys_config.h"
#include "sys/sys_event.h"
#include "sys/sys_sleep_scheduler.h"

namespace
{
    volatile NetServiceState s_state = NetServiceState::Disconnected;
    volatile NetSessionOutcome s_lastOutcome = NetSessionOutcome::None;
    TaskHandle_t s_daemonTaskHandle = nullptr;
    QueueHandle_t s_sessionQueue = nullptr;
    volatile bool s_workerActive = false;
    volatile bool s_abortRequested = false;
    volatile NetServiceState s_abortTargetState = NetServiceState::SyncFailed;

    volatile bool s_bootSyncPending = false;
    uint32_t s_bootSyncDueMs = 0;
    uint32_t s_sessionStartedMs = 0;
    uint32_t s_nextCommonCycleDueMs = 0;
    volatile bool s_keepOnlineRequested = false;
    uint32_t s_onlineTaskSequence = 0;
    NetOnlineTaskResult s_lastOnlineTaskResult;
    portMUX_TYPE s_onlineTaskResultMux = portMUX_INITIALIZER_UNLOCKED;
    volatile int64_t s_currentNetworkEpoch = 0;

    /* 连接、三台 NTP 服务器及有限数量业务任务共用的最后保险。 */
    constexpr uint32_t kSessionTotalTimeoutMs = 60UL * 1000UL;
    constexpr uint32_t kDisconnectedCycleIntervalMs = 15UL * 60UL * 1000UL;
    constexpr uint32_t kConnectedCycleIntervalMs = 5UL * 60UL * 1000UL;
    constexpr uint32_t kCommonCycleRetryAfterFailMs = 5UL * 60UL * 1000UL;
    constexpr uint32_t kTlsSettleAfterNtpMs = 250;
    constexpr size_t kExternalMallocInternalThresholdBytes = 1024;

    /* 实机内部连续堆限制下，网络任务固定使用 7 KiB 栈；HTTP/JSON 大对象均在堆上。 */
    constexpr uint32_t kNetworkTaskStackBytes = 7U * 1024U;
    constexpr size_t kWifiSsidCapacity = 33;
    constexpr size_t kWifiPasswordCapacity = 65;

    /**
     * 公共 NetSessionRequest 不携带密码；真正投递到 Core 0 时再把当时的 WiFi 配置复制进固定数组。
     * 这样手机在本轮联网期间更新 sysConfig，也不会让后台任务读取正在变动的 Arduino String。
     */
    struct QueuedSession
    {
        NetSessionRequest request;
        bool online_task = false;
        bool reuse_wifi = false;
        uint16_t online_task_id = 0;
        char ssid[kWifiSsidCapacity] = {};
        char password[kWifiPasswordCapacity] = {};
    };

    bool StateBlocksSleep(NetServiceState state)
    {
        return s_workerActive ||
               state == NetServiceState::Connecting ||
               state == NetServiceState::SyncingTime ||
               state == NetServiceState::RunningTasks ||
               state == NetServiceState::SyncSuccess;
    }

    void SetState(NetServiceState state)
    {
        s_state = state;
        SysSleep_SetBlocker(SysSleepBlocker::Network, StateBlocksSleep(state));
    }

    bool IsAbortRequested()
    {
        return s_abortRequested;
    }

    /**
     * 同时维护清醒主循环与 Light Sleep 的下一周期截止时间。网络周期可能拉取立即消息，且必须
     * 返回主循环启动 WiFi，因此使用 Foreground 唤醒；完成或失败后必须通过本函数重建截止时间。
     */
    void ScheduleNextCommonCycle(uint32_t delay_ms)
    {
        s_nextCommonCycleDueMs = millis() + delay_ms;
        if (sysConfig.wifi_ssid.isEmpty() || sysConfig.wifi_ssid == "Your_WiFi_Name")
        {
            /* 未配网设备不登记休眠唤醒，否则截止后会被一个永远无法执行的计划反复叫醒。 */
            SysSleep_Cancel(SysSleepSource::NetworkCycle);
            return;
        }
        SysSleep_ScheduleAfterMs(
            SysSleepSource::NetworkCycle,
            delay_ms,
            SysSleepWakeAction::Foreground);
    }

    /** 在线任务结果跨 Core 0 与主循环共享，使用短临界区保证快照字段来自同一次完成事件。 */
    void PublishOnlineTaskResult(uint16_t task_id, bool task_found, NetTaskDisposition disposition)
    {
        portENTER_CRITICAL(&s_onlineTaskResultMux);
        s_lastOnlineTaskResult.task_id = task_id;
        s_lastOnlineTaskResult.task_found = task_found;
        s_lastOnlineTaskResult.disposition = disposition;
        s_lastOnlineTaskResult.sequence = ++s_onlineTaskSequence;
        portEXIT_CRITICAL(&s_onlineTaskResultMux);
    }

    /** 占位符 SSID 不算有效配置，否则每次开机会无意义地申请约 60 KiB WiFi 内部堆。 */
    bool IsWifiConfigured()
    {
        return !sysConfig.wifi_ssid.isEmpty() &&
               sysConfig.wifi_ssid != "Your_WiFi_Name";
    }

    bool ConnectWifi(const QueuedSession &session)
    {
        SetState(NetServiceState::Connecting);
        Serial.println("[网络服务] 守护任务已唤醒，开始连接 WiFi。 ");

        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(true, false);
        vTaskDelay(pdMS_TO_TICKS(120));

        WiFi.mode(WIFI_STA);
        WiFi.begin(session.ssid, session.password);

        uint8_t timeout_ticks = 0;
        while (!s_abortRequested && WiFi.status() != WL_CONNECTED && timeout_ticks < 20)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            ++timeout_ticks;
        }
        return WiFi.status() == WL_CONNECTED;
    }

    void FailAndShutdown(NetServiceState state)
    {
        s_sessionStartedMs = 0;
        ScheduleNextCommonCycle(kCommonCycleRetryAfterFailMs);
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        NetAuthSession_Clear();
        s_workerActive = false;
        SetState(state);
    }

    void OnWifiSet(void *payload)
    {
        Evt_WifiSet_t *request = static_cast<Evt_WifiSet_t *>(payload);
        if (!request || !request->ssid || String(request->ssid).isEmpty())
        {
            SysCmdResult_Error("EMPTY_SSID");
            return;
        }

        const size_t ssid_length = strlen(request->ssid);
        const size_t password_length = request->pass ? strlen(request->pass) : 0;
        if (ssid_length >= kWifiSsidCapacity || password_length >= kWifiPasswordCapacity)
        {
            Serial.println("[网络服务] WiFi 配置被拒绝：SSID 或密码长度超过设备上限。 ");
            SysCmdResult_Error("WIFI_TOO_LONG");
            return;
        }

        sysConfig.wifi_ssid = String(request->ssid);
        sysConfig.wifi_pass = request->pass ? String(request->pass) : String();
        sysConfig.save();

        Serial.printf("[网络服务] WiFi 配置已保存，SSID=%s，开始公共联网周期。\n", request->ssid);
        SysCmdResult_Ok("SAVED", sysConfig.wifi_ssid);
        NetService_StartCommonCycle(false);
    }

    void MergeSummary(NetTaskRunSummary &target, const NetTaskRunSummary &source)
    {
        target.completed += source.completed;
        target.retried += source.retried;
        target.permanent_failed += source.permanent_failed;
        target.auth_blocked += source.auth_blocked;
        target.missing += source.missing;
        target.budget_exhausted = target.budget_exhausted || source.budget_exhausted;
    }

    NetTaskRunSummary RunRequestedTasks(const NetSessionRequest &request, const NetTaskContext &context)
    {
        NetTaskRunSummary summary;
        SetState(NetServiceState::RunningTasks);

        /* 已绑定设备的本轮 Token 必须先于认证型持久任务建立；首次绑定不在这里自动执行。 */
        MergeSummary(
            summary,
            NetTaskRegistry_ExecuteTriggeredTasks(NET_TASK_TRIGGER_SESSION_PREPARE, context));

        if (request.drain_outbox)
            MergeSummary(summary, NetTaskRegistry_DrainOutbox(context));

        MergeSummary(summary, NetTaskRegistry_ExecuteTriggeredTasks(request.task_trigger_mask, context));

        for (uint8_t index = 0; index < request.task_count && !s_abortRequested; ++index)
        {
            if (!context.CanStart())
            {
                summary.budget_exhausted = true;
                break;
            }

            /* 同一任务已经被本轮触发标签覆盖时不再显式执行，避免重复 GET/POST。 */
            if (NetTaskRegistry_TaskMatchesTrigger(request.task_ids[index], request.task_trigger_mask))
                continue;

            NetTaskExecutionResult result;
            if (!NetTaskRegistry_ExecuteSessionTask(request.task_ids[index], context, &result))
            {
                ++summary.missing;
                continue;
            }

            switch (result.disposition)
            {
            case NetTaskDisposition::Complete:
                ++summary.completed;
                break;
            case NetTaskDisposition::Retry:
                ++summary.retried;
                break;
            case NetTaskDisposition::PermanentFailure:
                ++summary.permanent_failed;
                break;
            case NetTaskDisposition::AuthBlocked:
                ++summary.auth_blocked;
                break;
            }

            /* 会话任务不是持久任务：失败只记录，本轮不会原地重试；下次策略触发时再执行。 */
            if (result.disposition != NetTaskDisposition::Complete)
            {
                Serial.printf("[网络服务] 会话任务 ID=%u 未完成，等待下一轮策略触发。\n",
                              static_cast<unsigned>(request.task_ids[index]));
            }
        }
        return summary;
    }

    void NetworkDaemonTask(void *)
    {
        while (true)
        {
            QueuedSession queued;
            if (!s_sessionQueue || xQueueReceive(s_sessionQueue, &queued, portMAX_DELAY) != pdTRUE)
                continue;

            /* 常驻 WiFi 会话上的显式任务：不重连、不校时，只复用当前认证上下文。 */
            if (queued.online_task)
            {
                if (WiFi.status() != WL_CONNECTED || s_state != NetServiceState::SyncSuccess)
                {
                    Serial.println("[网络服务] 在线任务执行时 WiFi 已断开，拒绝继续执行。 ");
                    PublishOnlineTaskResult(queued.online_task_id, false, NetTaskDisposition::Retry);
                    FailAndShutdown(s_abortRequested ? s_abortTargetState : NetServiceState::ConnectFailed);
                    continue;
                }

                s_workerActive = true;
                SetState(NetServiceState::RunningTasks);
                NetTaskContext context;
                context.network_epoch = s_currentNetworkEpoch;
                context.deadline_ms = millis() + 20UL * 1000UL;
                context.is_abort_requested = IsAbortRequested;
                NetTaskExecutionResult result;
                const bool found = NetTaskRegistry_ExecuteSessionTask(
                    queued.online_task_id, context, &result);
                const NetTaskDisposition disposition = found
                                                           ? result.disposition
                                                           : NetTaskDisposition::PermanentFailure;
                PublishOnlineTaskResult(queued.online_task_id, found, disposition);
                Serial.printf("[网络服务] 在线任务完成：ID=%u，结果=%u。\n",
                              static_cast<unsigned>(queued.online_task_id),
                              static_cast<unsigned>(disposition));
                if (s_abortRequested || WiFi.status() != WL_CONNECTED)
                    FailAndShutdown(s_abortRequested ? s_abortTargetState : NetServiceState::ConnectFailed);
                else
                {
                    s_workerActive = false;
                    SetState(NetServiceState::SyncSuccess);
                }
                continue;
            }

            if (queued.ssid[0] == '\0')
            {
                Serial.println("[网络服务] 未配置真实 WiFi，跳过会话。 ");
                ScheduleNextCommonCycle(kCommonCycleRetryAfterFailMs);
                s_sessionStartedMs = 0;
                s_workerActive = false;
                SetState(NetServiceState::Disconnected);
                continue;
            }

            const bool can_reuse_wifi = queued.reuse_wifi && WiFi.status() == WL_CONNECTED;
            if (can_reuse_wifi)
            {
                Serial.println("[网络服务] 复用常驻 WiFi，开始下一轮公共联网周期。 ");
            }
            else if (!ConnectWifi(queued))
            {
                Serial.println("[网络服务] WiFi 连接超时。 ");
                FailAndShutdown(s_abortRequested ? s_abortTargetState : NetServiceState::ConnectFailed);
                continue;
            }

            /* 每轮公共周期都重新校时并换取 Token，常驻 WiFi 也不沿用上一轮的认证上下文。 */
            NetAuthSession_Clear();

            SetState(NetServiceState::SyncingTime);
            time_t network_epoch = 0;
            if (!NetNtp_Sync(network_epoch, IsAbortRequested))
            {
                Serial.println("[网络服务] 基础 NTP 校时失败，本轮不执行普通任务。 ");
                FailAndShutdown(s_abortRequested ? s_abortTargetState : NetServiceState::SyncFailed);
                continue;
            }

            if (s_abortRequested)
            {
                FailAndShutdown(s_abortTargetState);
                continue;
            }

            /*
             * NTP 使用 UDP，HTTPS 紧接着建立 TCP/TLS。给协议栈一个很短的调度窗口，
             * 让 UDP socket 释放和系统时间更新完成传播；它不承担失败重试，也不改变会话语义。
             */
            vTaskDelay(pdMS_TO_TICKS(kTlsSettleAfterNtpMs));

            NetTaskContext context;
            context.network_epoch = static_cast<int64_t>(network_epoch);
            s_currentNetworkEpoch = context.network_epoch;
            context.deadline_ms = s_sessionStartedMs + kSessionTotalTimeoutMs;
            context.is_abort_requested = IsAbortRequested;
            const NetTaskRunSummary summary = RunRequestedTasks(queued.request, context);
            if (s_abortRequested)
            {
                FailAndShutdown(s_abortTargetState);
                continue;
            }

            s_lastOutcome = summary.HasErrors()
                                ? NetSessionOutcome::CompletedWithErrors
                                : NetSessionOutcome::Succeeded;
            SetState(NetServiceState::SyncSuccess);
            s_sessionStartedMs = 0;
            ScheduleNextCommonCycle(
                queued.request.keep_alive ? kConnectedCycleIntervalMs : kDisconnectedCycleIntervalMs);

            Serial.println("[网络服务] 本轮会话完成。 ");
            vTaskDelay(pdMS_TO_TICKS(2000));

            if (s_abortRequested)
            {
                FailAndShutdown(s_abortTargetState);
            }
            else if (!queued.request.keep_alive)
            {
                Serial.println("[网络服务] 会话结束，关闭 WiFi。 ");
                WiFi.disconnect(true, false);
                WiFi.mode(WIFI_OFF);
                NetAuthSession_Clear();
                s_workerActive = false;
                SetState(NetServiceState::Disconnected);
            }
            else
            {
                s_workerActive = false;
                SetState(NetServiceState::SyncSuccess);
                Serial.println("[网络服务] 手动连接模式，保持 WiFi 在线；5 分钟后执行下一轮公共联网周期。 ");
            }
        }
    }

    bool ValidateSession(const NetSessionRequest &request)
    {
        if (request.task_count > NET_SESSION_TASK_CAPACITY)
            return false;
        for (uint8_t index = 0; index < request.task_count; ++index)
        {
            if (request.task_ids[index] == 0)
                return false;
            for (uint8_t previous = 0; previous < index; ++previous)
            {
                if (request.task_ids[index] == request.task_ids[previous])
                    return false;
            }
        }
        return true;
    }

    /**
     * 常驻连接到达周期截止时间时，不经过对外 StartSession 的“忙碌”判定，直接把同一份公共
     * 周期请求投递给 Core 0。若 WiFi 已在两轮之间掉线，守护任务会使用随请求复制的配置重连。
     */
    bool QueueConnectedCommonCycle()
    {
        if (!s_daemonTaskHandle || !s_sessionQueue || s_workerActive ||
            s_state != NetServiceState::SyncSuccess || !s_keepOnlineRequested)
            return false;

        QueuedSession queued;
        queued.request.keep_alive = true;
        queued.request.drain_outbox = true;
        queued.request.task_trigger_mask = NET_TASK_TRIGGER_COMMON_CYCLE;
        queued.reuse_wifi = true;
        strlcpy(queued.ssid, sysConfig.wifi_ssid.c_str(), sizeof(queued.ssid));
        strlcpy(queued.password, sysConfig.wifi_pass.c_str(), sizeof(queued.password));

        s_abortRequested = false;
        s_abortTargetState = NetServiceState::SyncFailed;
        s_lastOutcome = NetSessionOutcome::None;
        s_sessionStartedMs = millis();
        s_workerActive = true;
        SetState(NetServiceState::SyncingTime);

        if (xQueueSend(s_sessionQueue, &queued, 0) != pdTRUE)
        {
            s_sessionStartedMs = 0;
            s_workerActive = false;
            ScheduleNextCommonCycle(kCommonCycleRetryAfterFailMs);
            SetState(NetServiceState::SyncSuccess);
            Serial.println("[网络服务] 常驻连接的公共联网周期入队失败，5 分钟后重试。 ");
            return false;
        }

        SysSleep_Cancel(SysSleepSource::NetworkCycle);
        Serial.println("[网络服务] 常驻连接已到 5 分钟周期，投递公共联网清单。 ");
        return true;
    }
}

void NetService_Init()
{
    SetState(NetServiceState::Disconnected);
    s_lastOutcome = NetSessionOutcome::None;
    s_onlineTaskSequence = 0;
    s_lastOnlineTaskResult.sequence = 0;
    s_lastOnlineTaskResult.task_id = 0;
    s_lastOnlineTaskResult.task_found = false;
    s_lastOnlineTaskResult.disposition = NetTaskDisposition::Retry;
    s_currentNetworkEpoch = 0;
    s_keepOnlineRequested = false;

    /*
     * Arduino 框架默认让不超过 4096 字节的普通 malloc 优先占用内部 SRAM，而当前设备同时
     * 常驻显示、音频、BLE、NFC 与网络任务，实机已经观察到 HTTPS 后内部连续块不足 3 KiB。
     * 这里复用 ESP-IDF 官方运行时策略，把大于 1 KiB 的普通缓冲优先放入 PSRAM：显式要求
     * MALLOC_CAP_INTERNAL/DMA 的 WiFi、lwIP 和硬件内存仍留在内部，不改编译环境和分区。
     */
    const size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (total_psram > 0)
    {
        heap_caps_malloc_extmem_enable(kExternalMallocInternalThresholdBytes);
        Serial.printf("[网络/内存] 已启用官方 PSRAM 分流：普通分配内部优先阈值=%u 字节。\n",
                      static_cast<unsigned>(kExternalMallocInternalThresholdBytes));
    }
    else
    {
        Serial.println("[网络/内存] 未检测到 PSRAM，保持框架默认普通堆策略。 ");
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    /*
     * 必须在任何 WiFiClientSecure 对象出现前安装一次。该接口只改变 mbedTLS 自己的
     * 动态对象，不改变普通 malloc、TinyUSB、显示 DMA 或文件系统的内存归属。
     */
    NetTlsMemory_InstallAllocator();

    NetOutbox_Init();
    NetBuiltinTasks_RegisterAll();

    ScheduleNextCommonCycle(kDisconnectedCycleIntervalMs);
    SysEvent_Subscribe(EVT_WIFI_SET, OnWifiSet);

    s_sessionQueue = xQueueCreate(1, sizeof(QueuedSession));
    if (!s_sessionQueue)
    {
        Serial.println("[网络服务] 无法创建会话请求队列。 ");
        return;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        NetworkDaemonTask,
        "NetService",
        kNetworkTaskStackBytes,
        nullptr,
        1,
        &s_daemonTaskHandle,
        0);
    if (created != pdPASS)
    {
        s_daemonTaskHandle = nullptr;
        Serial.printf("[网络服务] 后台任务创建失败；内部堆=%u，最大内部块=%u。\n",
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    }
}

bool NetService_StartSession(const NetSessionRequest &request)
{
    if (!ValidateSession(request))
    {
        Serial.println("[网络服务] 会话请求无效：任务数量、ID 或重复项不合法。 ");
        return false;
    }
    if (!s_daemonTaskHandle || !s_sessionQueue)
    {
        Serial.println("[网络服务] 会话请求被忽略：守护任务尚未创建。 ");
        return false;
    }
    if (!IsWifiConfigured())
    {
        Serial.println("[网络服务] 会话请求被忽略：尚未配置真实 WiFi。 ");
        SetState(NetServiceState::ConnectFailed);
        return false;
    }
    if (sysConfig.wifi_ssid.length() >= kWifiSsidCapacity ||
        sysConfig.wifi_pass.length() >= kWifiPasswordCapacity)
    {
        Serial.println("[网络服务] 会话请求被拒绝：WiFi SSID 或密码长度超过协议上限。 ");
        SetState(NetServiceState::ConnectFailed);
        return false;
    }
    if (NetService_IsBusy())
    {
        Serial.println("[网络服务] 会话请求被忽略：当前会话正在运行或保持在线。 ");
        return false;
    }

    s_bootSyncPending = false;
    s_abortRequested = false;
    s_abortTargetState = NetServiceState::SyncFailed;
    s_keepOnlineRequested = request.keep_alive;
    s_lastOutcome = NetSessionOutcome::None;
    s_sessionStartedMs = millis();
    s_workerActive = true;
    SetState(NetServiceState::Connecting);

    QueuedSession queued;
    queued.request = request;
    strlcpy(queued.ssid, sysConfig.wifi_ssid.c_str(), sizeof(queued.ssid));
    strlcpy(queued.password, sysConfig.wifi_pass.c_str(), sizeof(queued.password));

    if (xQueueSend(s_sessionQueue, &queued, 0) != pdTRUE)
    {
        s_workerActive = false;
        s_sessionStartedMs = 0;
        ScheduleNextCommonCycle(kCommonCycleRetryAfterFailMs);
        SetState(NetServiceState::SyncFailed);
        Serial.println("[网络服务] 会话请求入队失败，5 分钟后重试。 ");
        return false;
    }
    SysSleep_Cancel(SysSleepSource::NetworkCycle);
    return true;
}

bool NetService_StartCommonCycle(bool keep_alive)
{
    NetSessionRequest request;
    request.keep_alive = keep_alive;
    request.drain_outbox = true;
    request.task_trigger_mask = NET_TASK_TRIGGER_COMMON_CYCLE;
    return NetService_StartSession(request);
}

bool NetService_StartOnlineTask(uint16_t task_id)
{
    if (task_id == 0 || !s_daemonTaskHandle || !s_sessionQueue)
        return false;
    if (WiFi.status() != WL_CONNECTED || s_state != NetServiceState::SyncSuccess || s_workerActive)
    {
        Serial.println("[网络服务] 在线任务被拒绝：当前没有可复用的常驻 WiFi 会话。 ");
        return false;
    }

    QueuedSession queued;
    queued.online_task = true;
    queued.online_task_id = task_id;
    /* 必须先占用 worker 标记再唤醒 Core 0，防止极快任务完成后被主循环重新写成 busy。 */
    s_workerActive = true;
    if (xQueueSend(s_sessionQueue, &queued, 0) != pdTRUE)
    {
        s_workerActive = false;
        Serial.println("[网络服务] 在线任务入队失败。 ");
        return false;
    }
    Serial.printf("[网络服务] 已投递在线任务：ID=%u。\n", static_cast<unsigned>(task_id));
    return true;
}

NetOnlineTaskResult NetService_GetLastOnlineTaskResult()
{
    NetOnlineTaskResult result;
    portENTER_CRITICAL(&s_onlineTaskResultMux);
    result.sequence = s_lastOnlineTaskResult.sequence;
    result.task_id = s_lastOnlineTaskResult.task_id;
    result.task_found = s_lastOnlineTaskResult.task_found;
    result.disposition = s_lastOnlineTaskResult.disposition;
    portEXIT_CRITICAL(&s_onlineTaskResultMux);
    return result;
}

bool NetService_IsOnline()
{
    return WiFi.status() == WL_CONNECTED &&
           (s_state == NetServiceState::SyncSuccess ||
            s_state == NetServiceState::RunningTasks);
}

NetServiceState NetService_GetState()
{
    return s_state;
}

NetSessionOutcome NetService_GetLastOutcome()
{
    return s_lastOutcome;
}

bool NetService_IsBusy()
{
    return StateBlocksSleep(s_state);
}

void NetService_RequestBootSync(uint32_t delay_ms)
{
    if (!IsWifiConfigured())
    {
        Serial.println("[网络服务] 未配置真实 WiFi，跳过开机自动同步。 ");
        return;
    }

    s_bootSyncPending = true;
    s_bootSyncDueMs = millis() + delay_ms;
    Serial.printf("[网络服务] 开机公共联网周期将在 %lu ms 后触发。\n",
                  static_cast<unsigned long>(delay_ms));
}

void NetService_Update()
{
    const uint32_t now = millis();

    /* 各普通任务只在这里把 Core 0 结果落地到 UI、LittleFS 或 I2C 业务对象。 */
    NetTaskRegistry_Update();

    if (s_bootSyncPending && static_cast<int32_t>(now - s_bootSyncDueMs) >= 0)
    {
        s_bootSyncPending = false;
        Serial.println("[网络服务] 触发延迟开机公共联网周期。 ");
        NetService_StartCommonCycle(false);
    }

    if (NetService_IsBusy() &&
        s_sessionStartedMs > 0 &&
        now - s_sessionStartedMs > kSessionTotalTimeoutMs)
    {
        Serial.println("[网络服务] 会话总超时，强制中止。 ");
        NetService_Abort();
    }

    if (IsWifiConfigured() &&
        !s_bootSyncPending &&
        !s_workerActive &&
        static_cast<int32_t>(now - s_nextCommonCycleDueMs) >= 0)
    {
        /*
         * 手动常驻和普通定时联网共享同一周期。常驻状态复用 WiFi；其他空闲状态重新连接，
         * 但两者都会从 NTP 开始并执行完全相同的认证、Outbox 和例行任务清单。
         */
        if (s_state == NetServiceState::SyncSuccess && s_keepOnlineRequested)
        {
            QueueConnectedCommonCycle();
        }
        else if (s_state == NetServiceState::Disconnected ||
                 s_state == NetServiceState::ConnectFailed ||
                 s_state == NetServiceState::SyncFailed)
        {
            Serial.println("[网络服务] 已到 15 分钟周期，启动公共联网清单。 ");
            ScheduleNextCommonCycle(kCommonCycleRetryAfterFailMs);
            NetService_StartCommonCycle(s_keepOnlineRequested);
        }
    }
}

void NetService_Abort()
{
    s_bootSyncPending = false;
    s_sessionStartedMs = 0;
    ScheduleNextCommonCycle(kCommonCycleRetryAfterFailMs);
    s_abortTargetState = NetServiceState::SyncFailed;
    s_abortRequested = true;

    if (s_sessionQueue)
        xQueueReset(s_sessionQueue);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    NetAuthSession_Clear();
    SetState(NetServiceState::SyncFailed);
    Serial.println("[网络服务] 已强制关闭 WiFi，状态置为 SyncFailed。 ");
}

void NetService_Disconnect()
{
    s_bootSyncPending = false;
    s_sessionStartedMs = 0;
    s_keepOnlineRequested = false;
    s_abortTargetState = NetServiceState::Disconnected;
    s_abortRequested = true;

    if (s_sessionQueue)
        xQueueReset(s_sessionQueue);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    NetAuthSession_Clear();
    SetState(NetServiceState::Disconnected);
    Serial.println("[网络服务] 已按用户请求关闭 WiFi。 ");
}
