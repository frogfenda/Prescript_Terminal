/*
【模块职责】统一管理 Light Sleep 的进入条件和下一次定时唤醒计划。
【调用关系】业务模块只登记或取消自己的最近截止时间；Standby 只读取聚合计划，不了解闹钟、日程等业务数组。
【重要约束】绝对日历时间使用 UTC epoch，持续时长使用单调 millis 截止时间；接口可跨核心设置 blocker，截止时间仍应由主循环维护。
*/
#pragma once

#include <Arduino.h>
#include <time.h>

/** 可以登记定时唤醒的业务或系统来源；每个来源只保存一条最近截止时间。 */
enum class SysSleepSource : uint8_t
{
    RtcMaintenance = 0,
    Alarm,
    Schedule,
    Countdown,
    Pomodoro,
    AutoPush,
    Count,
};

/** 阻止进入 Light Sleep 的长事务来源。 */
enum class SysSleepBlocker : uint8_t
{
    Network = 0,
    NfcEmulation,
    Count,
};

/** 定时唤醒后的处理级别。前台任务必须完整恢复外设，静默维护完成后可以直接续睡。 */
enum class SysSleepWakeAction : uint8_t
{
    SilentMaintenance = 0,
    Foreground,
};

/** Standby 使用的聚合结果；delay_ms=0 表示至少一个来源已经到期。 */
struct SysSleepPlan
{
    uint64_t delay_ms;
    SysSleepWakeAction action;
};

/**
 * 登记绝对 UTC epoch 截止时间，适用于闹钟和日程等日历业务。
 * epoch 必须大于 0；无效值不会隐式取消旧计划，取消必须显式调用 SysSleep_Cancel()。
 */
void SysSleep_ScheduleEpoch(SysSleepSource source, time_t epoch, SysSleepWakeAction action);

/**
 * 从调用时刻起登记相对毫秒时长，适用于倒计时、番茄钟和周期维护。
 * delay_ms 会限制在 INT32_MAX 内，使 millis() 回绕后的有符号差值判断保持可靠；0 表示立即到期。
 */
void SysSleep_ScheduleAfterMs(SysSleepSource source, uint32_t delay_ms, SysSleepWakeAction action);

/** 取消某个来源尚未到期或已经到期的唤醒计划。 */
void SysSleep_Cancel(SysSleepSource source);

/**
 * 设置或解除休眠阻止状态。该接口内部使用临界区，可由网络/NFC 后台任务安全调用。
 * blocker 活跃时 Standby 不得开始新的 Light Sleep。
 */
void SysSleep_SetBlocker(SysSleepBlocker blocker, bool active);

/** 只要存在任意 blocker 就返回 false；不读取或修改定时截止时间。 */
bool SysSleep_CanEnter();

/**
 * 聚合所有有效来源，返回最近一次唤醒的剩余毫秒和处理级别。
 * 同一时刻既有静默维护又有前台业务时，Foreground 优先；没有计划时返回 false。
 */
bool SysSleep_GetPlan(SysSleepPlan *out_plan);

