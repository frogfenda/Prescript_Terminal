// 文件：src/sys/sys_network.h
/*
【模块职责】网络同步接口。

网络模块统一承担三种网络动作：
1. 开机自动完整同步：延迟触发 WiFi -> NTP -> API，获取时间和隐秘指令；
2. 手动完整同步：系统设置里的“同步网络时间/网络校时”，同样执行 NTP + API；
3. 周期轻量校时：只执行 WiFi -> NTP，不请求 API，用来修正本地走时漂移。

网络任务使用独立 UDP NTP 请求只取得 UTC epoch，不直接设置 ESP32 时钟；结果必须交给
SysTime 在主循环统一应用并写回 RTC。App 层不要直接调用 WiFi.begin()/WiFi.disconnect()，
只通过这里的接口请求动作和查询状态。
*/
#pragma once
#include <Arduino.h>

struct SysNetworkJob;

enum NetworkState {
    NET_DISCONNECTED,   // WiFi 关闭或网络任务空闲
    NET_CONNECTING,     // 正在连接 WiFi AP
    NET_SYNCING_NTP,    // WiFi 已连上，正在等待 NTP 返回时间
    NET_FETCHING_API,   // NTP 已成功，正在请求隐秘指令 API
    NET_SYNC_SUCCESS,   // 本轮网络动作完成，UI 可显示成功状态
    NET_CONNECT_FAILED, // WiFi 连接失败，例如 AP 不存在或密码错误
    NET_SYNC_FAILED     // NTP/API 失败，或总超时强制中止
};

/**
 * 初始化网络守护任务。
 *
 * 实现内容：
 * - 关闭 WiFi 射频，避免开机时自动占用系统资源；
 * - 注册 EVT_WIFI_SET，用于网页下发 WiFi 配置后自动完整同步；
 * - 创建固定在 Core 0 的网络后台任务。
 *
 * 注意：本函数只创建任务，不会立刻联网。
 */
void Network_Init();

/**
 * 启动一次完整网络同步。
 *
 * 流程：
 * WiFi 连接 -> NTP 对时 -> 请求隐秘指令 API -> 根据 keep_alive 决定是否断网。
 *
 * 使用场景：
 * - 开机延迟自动同步；
 * - 系统设置中的“同步网络时间”；
 * - 时间设置中的“网络校时”；
 * - 网页/BLE 下发 WIFI:ssid:password 后自动同步。
 *
 * keep_alive:
 * - false：完成后关闭 WiFi，适合自动同步；
 * - true：完成后保持 WiFi 在线，适合 WiFi 连接页的手动连接模式。
 */
void Network_StartSync(bool keep_alive = false);

/**
 * 启动一次轻量 NTP 校时。
 *
 * 流程：
 * WiFi 连接 -> NTP 对时 -> 关闭 WiFi。
 *
 * 它不访问隐秘指令 API，避免周期校时时反复请求服务器。
 * 该接口主要由 Network_Update() 的周期校时逻辑调用。
 */
void Network_StartTimeSyncOnly();

/** 联网待办执行结果。Complete 会删除任务；Retry 会保留并退避；PermanentFailure 进入死信。 */
enum class NetworkJobDisposition : uint8_t
{
    Complete,
    Retry,
    PermanentFailure,
};

struct NetworkJobExecutionResult
{
    NetworkJobDisposition disposition;
    uint32_t retry_after_seconds; // 仅 Retry 使用；0 表示采用网络模块的指数退避。

    NetworkJobExecutionResult(
        NetworkJobDisposition result = NetworkJobDisposition::Retry,
        uint32_t retry_seconds = 0)
        : disposition(result), retry_after_seconds(retry_seconds) {}
};

/**
 * 联网待办执行器。它运行在 Core 0 网络任务中，此时 WiFi 已连接且 NTP 已返回可信 UTC。
 *
 * 约束：
 * - 可以执行 HTTP/TLS 等网络 I/O；单项应自行设置有限超时；
 * - 不得直接修改 UI、LittleFS 业务对象或访问 Wire/I2C；需要落地的结果应投递回主循环；
 * - 必须把 job.job_id 一并发给服务器作为幂等键。
 */
using NetworkJobHandler = NetworkJobExecutionResult (*)(const SysNetworkJob &job, int64_t network_epoch);

/** setup 阶段注册 job_type 对应的执行器；同一类型不能重复注册。 */
bool Network_RegisterJobHandler(uint16_t job_type, NetworkJobHandler handler);

/**
 * 返回当前网络状态。
 *
 * WiFi 页面、网络同步页面、系统设置菜单会用该状态决定显示文字和是否允许重复操作。
 */
NetworkState Network_GetState();

/** 返回网络任务或保持在线阶段是否仍占用 WiFi；状态变化会同步登记统一休眠 blocker。 */
bool Network_IsBusy();

/** 统一关闭 WiFi 并清理网络状态；APP 不应直接操作 WiFi 或 g_state。 */
void Network_Disconnect();

/**
 * 请求一次延迟开机自动同步。
 *
 * 本函数只登记“未来某个时间点需要完整同步”，不会立刻启动 WiFi。
 * 真正的 Network_StartSync(false) 由 loop() 中的 Network_Update() 到点触发。
 */
void Network_RequestBootSync(uint32_t delay_ms);

/**
 * 网络模块的主循环维护函数。
 *
 * main.cpp 的 loop() 每轮调用一次。
 * 它很轻量，只做：
 * - 到点触发开机完整同步；
 * - 检查网络总超时兜底；
 * - 根据配置触发周期轻量 NTP 校时。
 * 真正联网成功后，Core 0 会在同一会话中领取并处理持久化 Outbox。
 *
 * 这里不直接执行 WiFi.begin()/HTTP/NTP 等耗时动作。
 */
void Network_Update();

/**
 * 强制中止当前网络动作。
 *
 * 用于总超时兜底：如果网络状态长时间停在 CONNECTING/NTP/API 阶段，
 * 主循环会调用本函数关闭 WiFi，并把状态置为失败。
 */
void Network_Abort();
