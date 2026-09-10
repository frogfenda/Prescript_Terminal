// 文件：src/net/includes/net/net_task_registry.h
/*
【模块职责】维护普通网络任务注册表，并统一执行会话任务与持久化 Outbox 任务。

同一个 task_id 只有一个执行器：
- 会话任务由 NetService 在本轮联网时直接点名执行；
- 持久任务由 NetOutbox 保存，下次联网时仍通过同一执行器执行。

这样新增业务不需要修改 WiFi/NTP 状态机，只需注册任务并选择“本次执行”或“持久入队”。
*/
#pragma once

#include <Arduino.h>

enum class NetTaskDisposition : uint8_t
{
    Complete,
    Retry,
    PermanentFailure,
    /** 设备尚未绑定或凭据暂不可用；保留持久任务，并停止本轮继续消耗认证任务。 */
    AuthBlocked,
};

struct NetTaskExecutionResult
{
    NetTaskDisposition disposition;
    uint32_t retry_after_seconds;

    NetTaskExecutionResult(
        NetTaskDisposition result = NetTaskDisposition::Retry,
        uint32_t retry_seconds = 0)
        : disposition(result), retry_after_seconds(retry_seconds) {}
};

/**
 * 执行器收到的统一任务视图。
 * persistent=false 表示本轮会话任务，此时 job_id=0、attempt_count=0。
 */
struct NetTaskInvocation
{
    bool persistent = false;
    uint64_t job_id = 0;
    uint8_t attempt_count = 0;
    const uint8_t *payload = nullptr;
    uint16_t payload_length = 0;
};

/**
 * 一次联网会话向任务暴露的只读上下文。deadline_ms 使用 millis() 时基，任务不得在剩余预算
 * 不足时再启动新的阻塞请求。
 */
struct NetTaskContext
{
    int64_t network_epoch = 0;
    uint32_t deadline_ms = 0;
    bool (*is_abort_requested)() = nullptr;

    uint32_t RemainingMs() const
    {
        const int32_t remaining = static_cast<int32_t>(deadline_ms - millis());
        return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
    }

    bool CanStart(uint32_t reserve_ms = 1000) const
    {
        return (!is_abort_requested || !is_abort_requested()) && RemainingMs() > reserve_ms;
    }
};

struct NetTaskRunSummary
{
    uint8_t completed = 0;
    uint8_t retried = 0;
    uint8_t permanent_failed = 0;
    uint8_t auth_blocked = 0;
    uint8_t missing = 0;
    bool budget_exhausted = false;

    bool HasErrors() const
    {
        return retried > 0 || permanent_failed > 0 || auth_blocked > 0 || missing > 0 || budget_exhausted;
    }
};

using NetTaskExecute = NetTaskExecutionResult (*)(const NetTaskInvocation &invocation, const NetTaskContext &context);
using NetTaskMainUpdate = void (*)();

enum class NetTaskAuthRequirement : uint8_t
{
    None,
    DeviceSession,
};

/** 会话触发标签可以按位组合；任务自行声明参与哪些会话策略。 */
constexpr uint8_t NET_TASK_TRIGGER_NONE = 0;
/** 每次 WiFi+NTP 成功后、Outbox 之前运行，用于绑定与建立本轮认证上下文。 */
constexpr uint8_t NET_TASK_TRIGGER_SESSION_PREPARE = 1U << 0;
constexpr uint8_t NET_TASK_TRIGGER_STANDARD_SYNC = 1U << 1;

struct NetTaskDefinition
{
    uint16_t task_id = 0;
    const char *name = nullptr;
    NetTaskExecute execute = nullptr;
    NetTaskMainUpdate main_update = nullptr;
    uint8_t trigger_mask = NET_TASK_TRIGGER_NONE;
    /** 需要设备会话的任务由注册表统一认证，执行器不直接接触永久通行码。 */
    NetTaskAuthRequirement auth_requirement = NetTaskAuthRequirement::None;
    /** 只有具备服务端幂等语义和版本化 payload 的任务才应打开。 */
    bool accepts_persistent = false;
};

/** setup 阶段注册任务；task_id 不能为 0，也不能重复。 */
bool NetTaskRegistry_Register(const NetTaskDefinition &definition);

/** 在 Core 0 网络任务中执行一个非持久化会话任务。 */
bool NetTaskRegistry_ExecuteSessionTask(
    uint16_t task_id,
    const NetTaskContext &context,
    NetTaskExecutionResult *out_result = nullptr);

/** 执行所有声明了指定触发标签的普通任务；状态机不需要知道具体任务 ID。 */
NetTaskRunSummary NetTaskRegistry_ExecuteTriggeredTasks(
    uint8_t trigger_mask,
    const NetTaskContext &context);

/** 在 Core 0 网络任务中领取并执行到期 Outbox；每轮有固定上限。 */
NetTaskRunSummary NetTaskRegistry_DrainOutbox(const NetTaskContext &context);

/** 判断指定任务是否已包含在某个触发标签中，供会话层避免“标签触发 + 显式点名”重复执行。 */
bool NetTaskRegistry_TaskMatchesTrigger(uint16_t task_id, uint8_t trigger_mask);

/** 在 Arduino 主循环调用各任务的 main_update，安全落地跨核结果。 */
void NetTaskRegistry_Update();
