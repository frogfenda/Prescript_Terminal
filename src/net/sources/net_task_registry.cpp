// 文件：src/net/sources/net_task_registry.cpp
/*
【模块职责】网络任务注册、分发以及持久化待办消费。
【并发边界】注册只发生在 setup；execute 固定在 Core 0；main_update 固定在主循环。
*/
#include "net/net_task_registry.h"
#include "net/net_auth_session.h"
#include "net/net_outbox.h"

namespace
{
    constexpr uint8_t kRegistryCapacity = 12;
    constexpr uint8_t kOutboxMaxJobsPerSession = 8;
    constexpr uint32_t kUnhandledRetrySeconds = 60UL * 60UL;
    constexpr uint32_t kAuthBlockedRetrySeconds = 5UL * 60UL;
    constexpr uint32_t kMinimumTaskStartBudgetMs = 1200;

    NetTaskDefinition s_definitions[kRegistryCapacity];

    const NetTaskDefinition *FindDefinition(uint16_t task_id)
    {
        for (const NetTaskDefinition &definition : s_definitions)
        {
            if (definition.task_id == task_id)
                return &definition;
        }
        return nullptr;
    }

    /** 默认指数退避从 30 秒开始，最多 6 小时。 */
    uint32_t DefaultRetrySeconds(uint8_t attempt_count)
    {
        const uint8_t exponent = attempt_count > 10 ? 10 : (attempt_count > 0 ? attempt_count - 1 : 0);
        const uint32_t delay_seconds = 30UL << exponent;
        const uint32_t maximum_seconds = 6UL * 60UL * 60UL;
        return delay_seconds > maximum_seconds ? maximum_seconds : delay_seconds;
    }

    void CountResult(const NetTaskExecutionResult &result, NetTaskRunSummary &summary)
    {
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
    }

    NetTaskExecutionResult ExecuteDefinition(
        const NetTaskDefinition &definition,
        const NetTaskInvocation &invocation,
        const NetTaskContext &context)
    {
        if (definition.auth_requirement == NetTaskAuthRequirement::DeviceSession)
        {
            const NetAuthEnsureResult auth = NetAuthSession_Ensure(context);
            if (auth == NetAuthEnsureResult::DeviceUnbound ||
                auth == NetAuthEnsureResult::CredentialsRejected)
                return {NetTaskDisposition::AuthBlocked, 0};
            if (auth != NetAuthEnsureResult::Ready)
                return {NetTaskDisposition::Retry, 5UL * 60UL};
        }
        return definition.execute(invocation, context);
    }
}

bool NetTaskRegistry_Register(const NetTaskDefinition &definition)
{
    if (definition.task_id == 0 || !definition.name || !definition.execute)
        return false;

    for (NetTaskDefinition &entry : s_definitions)
    {
        if (entry.task_id == definition.task_id)
        {
            Serial.printf("[网络任务] 注册失败：任务 ID=%u 已存在。\n",
                          static_cast<unsigned>(definition.task_id));
            return false;
        }
        if (entry.task_id == 0)
        {
            entry = definition;
            Serial.printf("[网络任务] 已注册：ID=%u，名称=%s。\n",
                          static_cast<unsigned>(definition.task_id),
                          definition.name);
            return true;
        }
    }

    Serial.println("[网络任务] 注册表已满。 ");
    return false;
}

bool NetTaskRegistry_ExecuteSessionTask(
    uint16_t task_id,
    const NetTaskContext &context,
    NetTaskExecutionResult *out_result)
{
    const NetTaskDefinition *definition = FindDefinition(task_id);
    if (!definition)
    {
        Serial.printf("[网络任务] 会话任务未注册：ID=%u。\n", static_cast<unsigned>(task_id));
        return false;
    }

    Serial.printf("[网络任务] 开始会话任务：ID=%u，名称=%s。\n",
                  static_cast<unsigned>(task_id), definition->name);
    const NetTaskInvocation invocation;
    const NetTaskExecutionResult result = ExecuteDefinition(*definition, invocation, context);
    if (out_result)
        *out_result = result;

    Serial.printf("[网络任务] 会话任务结束：ID=%u，结果=%u。\n",
                  static_cast<unsigned>(task_id),
                  static_cast<unsigned>(result.disposition));
    return true;
}

NetTaskRunSummary NetTaskRegistry_ExecuteTriggeredTasks(
    uint8_t trigger_mask,
    const NetTaskContext &context)
{
    NetTaskRunSummary summary;
    if (trigger_mask == NET_TASK_TRIGGER_NONE)
        return summary;

    for (const NetTaskDefinition &definition : s_definitions)
    {
        if (definition.task_id == 0 || (definition.trigger_mask & trigger_mask) == 0)
            continue;

        if (!context.CanStart(kMinimumTaskStartBudgetMs))
        {
            if (!context.is_abort_requested || !context.is_abort_requested())
                summary.budget_exhausted = true;
            break;
        }

        NetTaskExecutionResult result;
        if (!NetTaskRegistry_ExecuteSessionTask(definition.task_id, context, &result))
        {
            ++summary.missing;
            continue;
        }
        CountResult(result, summary);
        if (result.disposition != NetTaskDisposition::Complete)
            Serial.printf("[网络任务] 触发任务 ID=%u 未完成，等待下一轮策略触发。\n",
                          static_cast<unsigned>(definition.task_id));
    }
    return summary;
}

NetTaskRunSummary NetTaskRegistry_DrainOutbox(const NetTaskContext &context)
{
    NetTaskRunSummary summary;
    const NetOutboxStats initial_stats = NetOutbox_GetStats();
    if (!initial_stats.storage_ready || initial_stats.pending_count == 0)
        return summary;

    Serial.printf("[网络任务] 开始处理持久待办：本轮上限=%u，待处理=%u。\n",
                  static_cast<unsigned>(kOutboxMaxJobsPerSession),
                  static_cast<unsigned>(initial_stats.pending_count));

    for (uint8_t index = 0; index < kOutboxMaxJobsPerSession; ++index)
    {
        if (!context.CanStart(kMinimumTaskStartBudgetMs))
        {
            if (!context.is_abort_requested || !context.is_abort_requested())
                summary.budget_exhausted = true;
            break;
        }

        NetOutboxJob job;
        if (!NetOutbox_ClaimNextReady(context.network_epoch, &job))
            break;

        const NetTaskDefinition *definition = FindDefinition(job.task_id);
        if (!definition)
        {
            const bool saved = NetOutbox_Retry(job.job_id, context.network_epoch + kUnhandledRetrySeconds);
            ++summary.missing;
            Serial.printf("[网络任务] 持久任务 ID=%u 尚未注册，%s保留并延后 1 小时。\n",
                          static_cast<unsigned>(job.task_id),
                          saved ? "已" : "未能");
            if (!saved)
                break;
            continue;
        }

        if (!definition->accepts_persistent)
        {
            const bool saved = NetOutbox_DeadLetter(job.job_id);
            ++summary.permanent_failed;
            Serial.printf("[网络任务] 任务 ID=%u 不接受持久调用，%s转入死信。\n",
                          static_cast<unsigned>(job.task_id),
                          saved ? "已" : "未能");
            if (!saved)
                break;
            continue;
        }

        Serial.printf("[网络任务] 执行持久任务：ID=%u，名称=%s，尝试=%u，job=%08lX%08lX。\n",
                      static_cast<unsigned>(job.task_id),
                      definition->name,
                      static_cast<unsigned>(job.attempt_count),
                      static_cast<unsigned long>(job.job_id >> 32),
                      static_cast<unsigned long>(job.job_id));

        NetTaskInvocation invocation;
        invocation.persistent = true;
        invocation.job_id = job.job_id;
        invocation.attempt_count = job.attempt_count;
        invocation.payload = job.payload;
        invocation.payload_length = job.payload_length;
        const NetTaskExecutionResult result = ExecuteDefinition(*definition, invocation, context);
        CountResult(result, summary);

        bool saved = false;
        if (result.disposition == NetTaskDisposition::Complete)
        {
            saved = NetOutbox_Complete(job.job_id);
            Serial.printf("[网络任务] 持久任务%s确认完成。\n", saved ? "已" : "未能");
        }
        else if (result.disposition == NetTaskDisposition::PermanentFailure)
        {
            saved = NetOutbox_DeadLetter(job.job_id);
            Serial.printf("[网络任务] 持久任务发生永久错误，%s转入死信。\n", saved ? "已" : "未能");
        }
        else if (result.disposition == NetTaskDisposition::AuthBlocked)
        {
            saved = NetOutbox_Retry(job.job_id, context.network_epoch + kAuthBlockedRetrySeconds);
            Serial.printf("[网络任务] 设备认证暂不可用，%s保留任务并暂停本轮持久队列。\n",
                          saved ? "已" : "未能");
        }
        else
        {
            const uint32_t delay_seconds = result.retry_after_seconds > 0
                                               ? result.retry_after_seconds
                                               : DefaultRetrySeconds(job.attempt_count);
            saved = NetOutbox_Retry(job.job_id, context.network_epoch + delay_seconds);
            Serial.printf("[网络任务] 持久任务暂时失败，%s设置 %lu 秒后重试。\n",
                          saved ? "已" : "未能",
                          static_cast<unsigned long>(delay_seconds));
        }

        /* 状态提交失败时停止，避免同一个任务在单次会话内紧密重放。 */
        if (!saved)
            break;
        if (result.disposition == NetTaskDisposition::AuthBlocked)
            break;
    }
    return summary;
}

bool NetTaskRegistry_TaskMatchesTrigger(uint16_t task_id, uint8_t trigger_mask)
{
    const NetTaskDefinition *definition = FindDefinition(task_id);
    return definition && trigger_mask != NET_TASK_TRIGGER_NONE &&
           (definition->trigger_mask & trigger_mask) != 0;
}

void NetTaskRegistry_Update()
{
    for (const NetTaskDefinition &definition : s_definitions)
    {
        if (definition.task_id != 0 && definition.main_update)
            definition.main_update();
    }
}
