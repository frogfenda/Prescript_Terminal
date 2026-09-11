// 文件：src/net/includes/net/net_service.h
/*
【模块职责】顶层网络服务入口与联网会话编排。

一次联网会话固定经过：WiFi 连接 -> NTP 基础校时 -> 已绑定设备认证准备 -> Outbox -> 本轮普通任务 -> 断网/保持在线。
NetService 不包含具体 HTTP 业务。新增网络业务应注册到 NetTaskRegistry，再由会话 task_ids 点名
或写入 NetOutbox 等到下次联网执行。
*/
#pragma once

#include <Arduino.h>
#include "net/net_task_registry.h"

enum class NetServiceState : uint8_t
{
    Disconnected,
    Connecting,
    SyncingTime,
    RunningTasks,
    SyncSuccess,
    ConnectFailed,
    SyncFailed,
};

/** 最近一轮会话的业务结果；网络连通不再等同于所有服务器任务都成功。 */
enum class NetSessionOutcome : uint8_t
{
    None,
    Succeeded,
    CompletedWithErrors,
};

constexpr uint8_t NET_SESSION_TASK_CAPACITY = 4;

/** 主循环投递给 Core 0 的不可变会话请求。 */
struct NetSessionRequest
{
    bool keep_alive = false;
    bool drain_outbox = true;
    uint8_t task_trigger_mask = 0;
    uint8_t task_count = 0;
    uint16_t task_ids[NET_SESSION_TASK_CAPACITY] = {};
};

/**
 * 显式在线任务的最近一次完成结果。
 * sequence 每完成一次任务递增，页面可在投递前记录旧值，再据此区分历史结果与本次结果。
 */
struct NetOnlineTaskResult
{
    uint32_t sequence = 0;
    uint16_t task_id = 0;
    bool task_found = false;
    NetTaskDisposition disposition = NetTaskDisposition::Retry;
};

/** 初始化存储、内置网络任务、WiFi 事件订阅和 Core 0 守护任务；不会立即联网。 */
void NetService_Init();

/**
 * 请求自定义联网会话。
 * NTP 是所有会话的必经基础阶段，不需要也不允许作为 task_id 传入。
 */
bool NetService_StartSession(const NetSessionRequest &request);

/** 请求标准同步：校时、消费 Outbox、执行所有声明 STANDARD_SYNC 标签的任务。 */
bool NetService_StartStandardSync(bool keep_alive = false);

/** 请求轻量校时：仍会顺带消费 Outbox，但不执行额外会话任务。 */
bool NetService_StartTimeSyncOnly();

/**
 * 在手动保持在线的 WiFi 会话上执行一个非持久化任务。
 * 本入口不会重连 WiFi、不会重复 NTP，也不会消费 Outbox；任务仍固定在 Core 0 执行。
 */
bool NetService_StartOnlineTask(uint16_t task_id);

/** 返回最近完成的显式在线任务结果；调用方用 sequence 判断是否为本次请求。 */
NetOnlineTaskResult NetService_GetLastOnlineTaskResult();

/** WiFi 当前实际已连接；RunningTasks 期间也视为在线。 */
bool NetService_IsOnline();

NetServiceState NetService_GetState();

/** 返回最近完成一轮会话的汇总结果；会话进行中保持上一轮结果。 */
NetSessionOutcome NetService_GetLastOutcome();

/** 网络会话正在运行或 WiFi 正处于手动保持在线状态。 */
bool NetService_IsBusy();

/** 统一关闭 WiFi 并清理会话状态。 */
void NetService_Disconnect();

/** 登记延迟开机标准同步，避免 setup/首屏阶段直接联网。 */
void NetService_RequestBootSync(uint32_t delay_ms);

/** 主循环维护：落地任务结果、触发延迟/周期会话并执行总超时兜底。 */
void NetService_Update();

/** 强制中止当前会话，状态置为 SyncFailed。 */
void NetService_Abort();
