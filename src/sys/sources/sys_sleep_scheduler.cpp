/*
【模块职责】保存固定容量的休眠截止时间与 blocker，并生成 Standby 可直接执行的最近唤醒计划。
【线程约束】所有共享表访问都经过 ESP32 临界区；临界区内只复制或写入简单值，不调用 time()、millis() 或业务代码。
*/
#include "sys/sys_sleep_scheduler.h"
#include "sys/sys_time.h"
#include <limits.h>

namespace
{
    enum class DeadlineClock : uint8_t
    {
        None = 0,
        Epoch,
        MonotonicMillis,
    };

    struct DeadlineSlot
    {
        DeadlineClock clock = DeadlineClock::None;
        uint64_t value = 0;
        SysSleepWakeAction action = SysSleepWakeAction::SilentMaintenance;
    };

    DeadlineSlot s_deadlines[(size_t)SysSleepSource::Count] = {};
    bool s_blockers[(size_t)SysSleepBlocker::Count] = {};
    portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

    bool ValidSource(SysSleepSource source)
    {
        return (size_t)source < (size_t)SysSleepSource::Count;
    }

    bool ValidBlocker(SysSleepBlocker blocker)
    {
        return (size_t)blocker < (size_t)SysSleepBlocker::Count;
    }
}

void SysSleep_ScheduleEpoch(SysSleepSource source, time_t epoch, SysSleepWakeAction action)
{
    if (!ValidSource(source) || epoch <= 0)
        return;

    portENTER_CRITICAL(&s_lock);
    DeadlineSlot &slot = s_deadlines[(size_t)source];
    slot.clock = DeadlineClock::Epoch;
    slot.value = (uint64_t)epoch;
    slot.action = action;
    portEXIT_CRITICAL(&s_lock);
}

void SysSleep_ScheduleAfterMs(SysSleepSource source, uint32_t delay_ms, SysSleepWakeAction action)
{
    if (!ValidSource(source))
        return;

    /* 单调截止时间必须位于当前时刻之后半个 uint32_t 周期内，才能用 int32_t 安全判断先后。 */
    if (delay_ms > (uint32_t)INT32_MAX)
        delay_ms = (uint32_t)INT32_MAX;
    uint32_t deadline_ms = millis() + delay_ms;

    portENTER_CRITICAL(&s_lock);
    DeadlineSlot &slot = s_deadlines[(size_t)source];
    slot.clock = DeadlineClock::MonotonicMillis;
    slot.value = deadline_ms;
    slot.action = action;
    portEXIT_CRITICAL(&s_lock);
}

void SysSleep_Cancel(SysSleepSource source)
{
    if (!ValidSource(source))
        return;

    portENTER_CRITICAL(&s_lock);
    s_deadlines[(size_t)source] = DeadlineSlot{};
    portEXIT_CRITICAL(&s_lock);
}

void SysSleep_SetBlocker(SysSleepBlocker blocker, bool active)
{
    if (!ValidBlocker(blocker))
        return;

    portENTER_CRITICAL(&s_lock);
    s_blockers[(size_t)blocker] = active;
    portEXIT_CRITICAL(&s_lock);
}

bool SysSleep_CanEnter()
{
    bool can_enter = true;
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < (size_t)SysSleepBlocker::Count; ++i)
    {
        if (s_blockers[i])
        {
            can_enter = false;
            break;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return can_enter;
}

bool SysSleep_GetPlan(SysSleepPlan *out_plan)
{
    if (!out_plan)
        return false;

    DeadlineSlot slots[(size_t)SysSleepSource::Count];
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < (size_t)SysSleepSource::Count; ++i)
        slots[i] = s_deadlines[i];
    portEXIT_CRITICAL(&s_lock);

    uint32_t now_ms = millis();
    /* 当前墙上时间只从 SysTime 读取；调度器不再直接拥有第二条 time() 读取路径。 */
    time_t now_epoch = SysTime_NowEpoch();
    bool found = false;
    uint64_t earliest_delay_ms = 0;
    SysSleepWakeAction earliest_action = SysSleepWakeAction::SilentMaintenance;

    for (size_t i = 0; i < (size_t)SysSleepSource::Count; ++i)
    {
        const DeadlineSlot &slot = slots[i];
        uint64_t delay_ms = 0;

        if (slot.clock == DeadlineClock::Epoch)
        {
            /* 系统时间尚未建立时不能把 epoch 换算成可靠延时；单调来源仍可照常参与聚合。 */
            if (now_epoch < 1000000000)
                continue;
            time_t epoch = (time_t)slot.value;
            delay_ms = epoch <= now_epoch ? 0 : (uint64_t)(epoch - now_epoch) * 1000ULL;
        }
        else if (slot.clock == DeadlineClock::MonotonicMillis)
        {
            int32_t remaining_ms = (int32_t)((uint32_t)slot.value - now_ms);
            delay_ms = remaining_ms <= 0 ? 0 : (uint32_t)remaining_ms;
        }
        else
        {
            continue;
        }

        if (!found || delay_ms < earliest_delay_ms)
        {
            found = true;
            earliest_delay_ms = delay_ms;
            earliest_action = slot.action;
        }
        else if (delay_ms == earliest_delay_ms && slot.action == SysSleepWakeAction::Foreground)
        {
            /* 多个来源同时到期时，用户提醒不能被静默维护掩盖。 */
            earliest_action = SysSleepWakeAction::Foreground;
        }
    }

    if (!found)
        return false;
    out_plan->delay_ms = earliest_delay_ms;
    out_plan->action = earliest_action;
    return true;
}
