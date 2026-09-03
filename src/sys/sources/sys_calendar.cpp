/*
【模块职责】日历事件调度实现。

本文件把持久化业务数据与硬件单槽闹钟明确分开：
- sysConfig 中的闹钟/日程是事实数据；
- occurrence 表是可随时重建的内存索引；
- PCF8563 闹钟只是下一次唤醒缓存，绝不作为第二份业务数据库；
- SysSleepScheduler 保留同一业务 epoch 作为 RTC 缺席或写入失败时的 ESP 定时器兜底。
*/
#include "sys/sys_calendar.h"
#include "bsp/bsp_pcf8563.h"
#include "sys/sys_config.h"
#include "sys/sys_constants.h"
#include "sys/sys_reminder.h"
#include "sys/sys_sleep_scheduler.h"
#include "sys/sys_time.h"

namespace
{
    constexpr size_t MAX_OCCURRENCES =
        PrescriptConst::MAX_SCHEDULES + PrescriptConst::MAX_ALARMS;
    constexpr uint32_t DUE_CHECK_INTERVAL_MS = 250;
    constexpr uint32_t RTC_RETRY_INTERVAL_MS = 5000;
    constexpr uint32_t RTC_ERROR_LOG_INTERVAL_MS = 60000;
    constexpr time_t EXPIRED_SCHEDULE_KEEP_SECONDS = 24 * 60 * 60;

    struct CalendarOccurrence
    {
        SysCalendarEventKind kind = SysCalendarEventKind::Schedule;
        uint8_t source_index = 0;
        time_t trigger_epoch = 0;
        uint16_t stable_id = 0;
    };

    enum class RtcPlanMode : uint8_t
    {
        Disabled = 0,
        DailyAlarm,
        DateAlarm,
        MonthCheckpoint,
    };

    struct RtcWakePlan
    {
        RtcPlanMode mode = RtcPlanMode::Disabled;
        time_t intended_epoch = 0;
        uint8_t day = 0;
        uint8_t hour = 0;
        uint8_t minute = 0;
    };

    CalendarOccurrence s_occurrences[MAX_OCCURRENCES] = {};
    size_t s_occurrence_count = 0;
    uint32_t s_data_generation = 1;
    uint32_t s_applied_data_generation = 0;
    uint32_t s_applied_time_revision = 0;
    uint32_t s_last_due_check_ms = 0;
    time_t s_last_alarm_triggered_minute = (time_t)-1;
    bool s_initialized = false;
    bool s_rebuild_requested = true;

    RtcWakePlan s_programmed_rtc_plan = {};
    bool s_programmed_rtc_plan_valid = false;
    uint32_t s_next_rtc_retry_ms = 0;
    uint32_t s_last_rtc_error_log_ms = 0;
    uint32_t s_applied_rtc_reset_generation = 0;

    SysCalendarStatus s_status = {
        false,
        false,
        SysCalendarEventKind::Schedule,
        0,
        0,
        false,
    };

    bool IsOccurrenceEarlier(const CalendarOccurrence &left, const CalendarOccurrence &right)
    {
        if (left.trigger_epoch != right.trigger_epoch)
            return left.trigger_epoch < right.trigger_epoch;
        if (left.kind != right.kind)
            return left.kind == SysCalendarEventKind::Schedule;
        return left.stable_id < right.stable_id;
    }

    void SortOccurrences()
    {
        /* 固定最多 25 项，插入排序无需堆分配，且相同时间的稳定优先级一目了然。 */
        for (size_t i = 1; i < s_occurrence_count; ++i)
        {
            CalendarOccurrence value = s_occurrences[i];
            size_t j = i;
            while (j > 0 && IsOccurrenceEarlier(value, s_occurrences[j - 1]))
            {
                s_occurrences[j] = s_occurrences[j - 1];
                --j;
            }
            s_occurrences[j] = value;
        }
    }

    bool SameRtcPlan(const RtcWakePlan &left, const RtcWakePlan &right)
    {
        return left.mode == right.mode &&
               left.intended_epoch == right.intended_epoch &&
               left.day == right.day &&
               left.hour == right.hour &&
               left.minute == right.minute;
    }

    void LogRtcFailureRateLimited(const char *message)
    {
        uint32_t now_ms = millis();
        if (s_last_rtc_error_log_ms == 0 ||
            (uint32_t)(now_ms - s_last_rtc_error_log_ms) >= RTC_ERROR_LOG_INTERVAL_MS)
        {
            Serial.println(message);
            s_last_rtc_error_log_ms = now_ms;
        }
    }

    bool RemoveExpiredSchedules(time_t now)
    {
        bool changed = false;
        for (int i = 0; i < sysConfig.schedule_count; ++i)
        {
            const ScheduleItem &item = sysConfig.schedules[i];
            bool remove = item.is_expired &&
                (item.is_hidden ||
                 (item.expire_time > 0 && now >= (time_t)item.expire_time + EXPIRED_SCHEDULE_KEEP_SECONDS));
            if (!remove)
                continue;

            for (int j = i; j < sysConfig.schedule_count - 1; ++j)
                sysConfig.schedules[j] = sysConfig.schedules[j + 1];
            --sysConfig.schedule_count;
            --i;
            changed = true;
        }
        return changed;
    }

    /** 旧 profile 若有多个活动的同分钟闹钟，只停用后出现的项目，不删除用户内容。 */
    bool DisableDuplicateActiveAlarms()
    {
        bool changed = false;
        for (int i = 0; i < sysConfig.alarm_count; ++i)
        {
            if (!sysConfig.alarms[i].is_active)
                continue;
            for (int j = 0; j < i; ++j)
            {
                if (sysConfig.alarms[j].is_active &&
                    sysConfig.alarms[j].hour == sysConfig.alarms[i].hour &&
                    sysConfig.alarms[j].min == sysConfig.alarms[i].min)
                {
                    sysConfig.alarms[i].is_active = false;
                    changed = true;
                    Serial.printf("[日历-警告] 检测到旧配置中的同分钟闹钟冲突，已停用：%s。\n",
                                  sysConfig.alarms[i].name.c_str());
                    break;
                }
            }
        }
        return changed;
    }

    bool HasDueBusinessEvent(const SysTimeSnapshot &snapshot)
    {
        for (int i = 0; i < sysConfig.schedule_count; ++i)
        {
            const ScheduleItem &item = sysConfig.schedules[i];
            if (!item.is_expired && item.target_time > 0 && snapshot.epoch >= (time_t)item.target_time)
                return true;
        }

        time_t current_minute = snapshot.epoch / 60;
        if (current_minute == s_last_alarm_triggered_minute)
            return false;
        for (int i = 0; i < sysConfig.alarm_count; ++i)
        {
            const AlarmPreset &alarm = sysConfig.alarms[i];
            if (alarm.is_active &&
                alarm.hour == snapshot.local.tm_hour &&
                alarm.min == snapshot.local.tm_min)
                return true;
        }
        return false;
    }

    /**
     * 执行到期业务时直接扫描事实数组，而不是依赖可能尚未重建的 occurrence 表。
     * 这样同一轮先处理数据变更/网络跳时也不会漏事件；提醒入队失败则保持原状态等待下轮重试。
     */
    bool ProcessDueEvents(const SysTimeSnapshot &snapshot)
    {
        bool data_changed = DisableDuplicateActiveAlarms();
        data_changed |= RemoveExpiredSchedules(snapshot.epoch);

        // 第一阶段：日程。所有到期日程都先于同分钟闹钟进入 SysReminder FIFO。
        for (int i = 0; i < sysConfig.schedule_count; ++i)
        {
            ScheduleItem &item = sysConfig.schedules[i];
            if (item.is_expired || item.target_time == 0 || snapshot.epoch < (time_t)item.target_time)
                continue;

            bool queued = item.prescript.length() == 0
                ? SysReminder_Submit(SysReminderKind::Random, nullptr, false)
                : SysReminder_Submit(SysReminderKind::Custom, item.prescript.c_str(), false);
            if (!queued)
                continue;

            item.is_expired = true;
            item.expire_time = (uint32_t)snapshot.epoch;
            item.is_restored = false;
            data_changed = true;
        }

        // 第二阶段：每日闹钟。同一分钟最多触发一个，精确 epoch 分钟键防止 AF 锁存和回环重复提醒。
        time_t current_minute = snapshot.epoch / 60;
        if (current_minute != s_last_alarm_triggered_minute)
        {
            for (int i = 0; i < sysConfig.alarm_count; ++i)
            {
                const AlarmPreset &alarm = sysConfig.alarms[i];
                if (!alarm.is_active ||
                    alarm.hour != snapshot.local.tm_hour ||
                    alarm.min != snapshot.local.tm_min)
                    continue;

                if (SysReminder_Submit(SysReminderKind::Custom, alarm.prescript.c_str(), false))
                {
                    s_last_alarm_triggered_minute = current_minute;
                    s_rebuild_requested = true;
                }
                break;
            }
        }

        if (data_changed)
        {
            /* 一轮内批量落盘一次；配置层只递增 generation，真正重建仍由本模块稍后统一完成。 */
            sysConfig.save();
            s_rebuild_requested = true;
        }
        return data_changed;
    }

    time_t BuildNextAlarmEpoch(const AlarmPreset &alarm, const SysTimeSnapshot &snapshot)
    {
        time_t candidate = 0;
        if (!SysTime_LocalDateTimeToEpoch(
                (uint16_t)(snapshot.local.tm_year + 1900),
                (uint8_t)(snapshot.local.tm_mon + 1),
                (uint8_t)snapshot.local.tm_mday,
                alarm.hour,
                alarm.min,
                0,
                &candidate))
            return 0;

        time_t candidate_minute = candidate / 60;
        time_t current_minute = snapshot.epoch / 60;
        if (candidate_minute < current_minute ||
            (candidate_minute == current_minute &&
             s_last_alarm_triggered_minute == current_minute))
            candidate += 24 * 60 * 60;
        return candidate;
    }

    void AddOccurrence(SysCalendarEventKind kind, uint8_t source_index, time_t epoch)
    {
        if (epoch <= 0 || s_occurrence_count >= MAX_OCCURRENCES)
            return;

        CalendarOccurrence &item = s_occurrences[s_occurrence_count++];
        item.kind = kind;
        item.source_index = source_index;
        item.trigger_epoch = epoch;
        item.stable_id = (uint16_t)(((uint16_t)kind << 8) | source_index);
    }

    void RebuildOccurrences(const SysTimeSnapshot &snapshot)
    {
        s_occurrence_count = 0;

        for (uint8_t i = 0; i < sysConfig.schedule_count; ++i)
        {
            const ScheduleItem &schedule = sysConfig.schedules[i];
            if (!schedule.is_expired && schedule.target_time > 0)
                AddOccurrence(SysCalendarEventKind::Schedule, i, (time_t)schedule.target_time);
        }

        for (uint8_t i = 0; i < sysConfig.alarm_count; ++i)
        {
            const AlarmPreset &alarm = sysConfig.alarms[i];
            if (alarm.is_active)
                AddOccurrence(SysCalendarEventKind::Alarm, i, BuildNextAlarmEpoch(alarm, snapshot));
        }

        SortOccurrences();
        s_status.occurrence_count = (uint8_t)s_occurrence_count;
        s_status.next_event_valid = s_occurrence_count > 0;
        if (s_occurrence_count > 0)
        {
            s_status.next_event_kind = s_occurrences[0].kind;
            s_status.next_event_epoch = s_occurrences[0].trigger_epoch;
            SysSleep_ScheduleEpoch(
                SysSleepSource::Calendar,
                s_occurrences[0].trigger_epoch,
                SysSleepWakeAction::Foreground);
        }
        else
        {
            s_status.next_event_epoch = 0;
            SysSleep_Cancel(SysSleepSource::Calendar);
        }
    }

    bool BuildMonthCheckpoint(const SysTimeSnapshot &snapshot, RtcWakePlan &plan)
    {
        uint16_t year = (uint16_t)(snapshot.local.tm_year + 1900);
        uint8_t month = (uint8_t)(snapshot.local.tm_mon + 1);
        if (++month > 12)
        {
            month = 1;
            ++year;
        }

        time_t epoch = 0;
        if (!SysTime_LocalDateTimeToEpoch(year, month, 1, 0, 0, 0, &epoch))
            return false;

        plan.mode = RtcPlanMode::MonthCheckpoint;
        plan.intended_epoch = epoch;
        plan.day = 1;
        plan.hour = 0;
        plan.minute = 0;
        return true;
    }

    RtcWakePlan BuildRtcPlan(const SysTimeSnapshot &snapshot)
    {
        RtcWakePlan plan = {};
        if (s_occurrence_count == 0)
            return plan;

        const CalendarOccurrence &next = s_occurrences[0];
        struct tm event_local = {};
        localtime_r(&next.trigger_epoch, &event_local);

        if (next.kind == SysCalendarEventKind::Alarm)
        {
            plan.mode = RtcPlanMode::DailyAlarm;
            plan.intended_epoch = next.trigger_epoch;
            plan.hour = (uint8_t)event_local.tm_hour;
            plan.minute = (uint8_t)event_local.tm_min;
            return plan;
        }

        bool same_month =
            event_local.tm_year == snapshot.local.tm_year &&
            event_local.tm_mon == snapshot.local.tm_mon;
        if (same_month)
        {
            plan.mode = RtcPlanMode::DateAlarm;
            plan.intended_epoch = next.trigger_epoch;
            plan.day = (uint8_t)event_local.tm_mday;
            plan.hour = (uint8_t)event_local.tm_hour;
            plan.minute = (uint8_t)event_local.tm_min;
            return plan;
        }

        /* RTC 无月份比较位：未来月份不能直接写日号，否则会在本月误唤醒，改写为下月 1 日检查点。 */
        BuildMonthCheckpoint(snapshot, plan);
        return plan;
    }

    bool ProgramRtcPlan(const RtcWakePlan &plan)
    {
        if (plan.mode == RtcPlanMode::Disabled)
            return BSP::Pcf8563::ClearAlarm();

        BSP::Pcf8563::AlarmConfig config = {};
        config.minute = plan.minute;
        config.hour = plan.hour;
        config.match_day = plan.mode != RtcPlanMode::DailyAlarm;
        config.day = plan.day;
        config.match_weekday = false;
        config.weekday = 0;
        return BSP::Pcf8563::ConfigureAlarm(config);
    }

    void ApplyRtcPlan(const RtcWakePlan &desired)
    {
        uint32_t now_ms = millis();
        bool same = s_programmed_rtc_plan_valid && SameRtcPlan(desired, s_programmed_rtc_plan);

        /* 同一个已写入计划无需反复访问 I2C；若 INT# 仍低则单独确认 AF，避免立即唤醒回环。 */
        if (same && !BSP::Pcf8563::IsInterruptAsserted())
        {
            s_status.rtc_plan_programmed = true;
            return;
        }

        if (!BSP::Pcf8563::IsReady())
        {
            s_programmed_rtc_plan_valid = false;
            s_status.rtc_plan_programmed = false;
            s_next_rtc_retry_ms = now_ms + RTC_RETRY_INTERVAL_MS;
            return;
        }

        if ((int32_t)(now_ms - s_next_rtc_retry_ms) < 0)
            return;

        if (same && BSP::Pcf8563::IsInterruptAsserted())
        {
            if (BSP::Pcf8563::AcknowledgeAlarm())
            {
                s_status.rtc_plan_programmed = true;
                return;
            }
        }
        else if (ProgramRtcPlan(desired))
        {
            s_programmed_rtc_plan = desired;
            s_programmed_rtc_plan_valid = true;
            s_status.rtc_plan_programmed = true;
            s_next_rtc_retry_ms = 0;
            return;
        }

        s_programmed_rtc_plan_valid = false;
        s_status.rtc_plan_programmed = false;
        s_next_rtc_retry_ms = now_ms + RTC_RETRY_INTERVAL_MS;
        LogRtcFailureRateLimited("[日历-警告] RTC 下一唤醒计划写入失败，暂由 ESP 定时器兜底。");
    }

    void DisablePlansForInvalidTime()
    {
        s_occurrence_count = 0;
        s_status.time_valid = false;
        s_status.next_event_valid = false;
        s_status.next_event_epoch = 0;
        s_status.occurrence_count = 0;
        SysSleep_Cancel(SysSleepSource::Calendar);
        ApplyRtcPlan(RtcWakePlan{});
    }
}

void SysCalendar_Init()
{
    s_initialized = true;
    s_applied_data_generation = 0;
    s_applied_time_revision = 0;
    s_last_due_check_ms = 0;
    s_last_alarm_triggered_minute = (time_t)-1;
    s_rebuild_requested = true;
    s_programmed_rtc_plan_valid = false;
    s_next_rtc_retry_ms = 0;
    s_last_rtc_error_log_ms = 0;
    s_applied_rtc_reset_generation = 0;
    s_status = {false, false, SysCalendarEventKind::Schedule, 0, 0, false};
}

void SysCalendar_Update()
{
    if (!s_initialized)
        return;

    SysTimeSnapshot snapshot = {};
    if (!SysTime_GetSnapshot(&snapshot) || !snapshot.valid)
    {
        if (s_status.time_valid || s_applied_data_generation != s_data_generation)
            DisablePlansForInvalidTime();
        s_applied_data_generation = s_data_generation;
        s_applied_time_revision = snapshot.revision;
        return;
    }

    s_status.time_valid = true;
    uint32_t now_ms = millis();

    /*
     * SysTime 在 RTC 离线恢复时会重新执行 BSP::Begin()，该过程按设计清空旧 AF/闹钟寄存器。
     * BSP 代号让日历缓存同步失效，避免“内存认为已写入、芯片实际已清空”的隐蔽漏唤醒。
     */
    uint32_t rtc_reset_generation = BSP::Pcf8563::GetAlarmResetGeneration();
    if (rtc_reset_generation != s_applied_rtc_reset_generation)
    {
        s_applied_rtc_reset_generation = rtc_reset_generation;
        s_programmed_rtc_plan_valid = false;
        s_rebuild_requested = true;
    }
    if (!BSP::Pcf8563::IsReady() && s_programmed_rtc_plan_valid)
    {
        s_programmed_rtc_plan_valid = false;
        s_status.rtc_plan_programmed = false;
        s_rebuild_requested = true;
    }

    /*
     * RTC 可能在设备亮屏期间命中月初检查点或闹钟。INT# 不依赖 HAL 唤醒路径也会保持低电平，
     * 因此主循环必须主动把它视为一次重建请求，随后改写下一槽或至少确认 AF。
     */
    if (BSP::Pcf8563::IsInterruptAsserted())
        s_rebuild_requested = true;

    if (s_last_due_check_ms == 0 ||
        (uint32_t)(now_ms - s_last_due_check_ms) >= DUE_CHECK_INTERVAL_MS)
    {
        s_last_due_check_ms = now_ms;
        ProcessDueEvents(snapshot);
    }

    bool generation_changed = s_applied_data_generation != s_data_generation;
    bool time_changed = s_applied_time_revision != snapshot.revision;
    if (generation_changed || time_changed || s_rebuild_requested)
    {
        RebuildOccurrences(snapshot);
        RtcWakePlan desired = BuildRtcPlan(snapshot);
        ApplyRtcPlan(desired);
        s_applied_data_generation = s_data_generation;
        s_applied_time_revision = snapshot.revision;
        s_rebuild_requested = false;
    }
    else if (!s_programmed_rtc_plan_valid && (int32_t)(now_ms - s_next_rtc_retry_ms) >= 0)
    {
        /* RTC 扩展板可能热插/接触恢复；不重建业务表，只按低频重试当前派生计划。 */
        ApplyRtcPlan(BuildRtcPlan(snapshot));
    }
}

bool SysCalendar_ServiceSleepWake()
{
    if (!s_initialized)
        return false;

    SysTimeSnapshot snapshot = {};
    if (!SysTime_GetSnapshot(&snapshot) || !snapshot.valid)
        return false;

    /*
     * 先记录业务是否到期，再强制维护。维护成功后会把闹钟推进到明天、把日程标成过期，
     * 因此不能在 Update 之后再反查“是否到期”，否则会把真实提醒误判成静默检查点。
     */
    bool foreground_required = HasDueBusinessEvent(snapshot);
    s_last_due_check_ms = 0;
    s_rebuild_requested = true;
    SysCalendar_Update();
    return foreground_required;
}

void SysCalendar_NotifyDataChanged()
{
    ++s_data_generation;
    if (s_data_generation == 0)
        s_data_generation = 1;
}

bool SysCalendar_GetStatus(SysCalendarStatus *out_status)
{
    if (!out_status)
        return false;
    *out_status = s_status;
    return true;
}
