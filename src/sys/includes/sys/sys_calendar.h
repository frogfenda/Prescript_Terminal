/*
【模块职责】统一维护闹钟与日程的派生触发表、到期顺序和下一次硬件/软件唤醒计划。
【调用关系】配置写入只标记 generation；Arduino 主循环在 SysTime_Update() 之后调用一次 SysCalendar_Update()。
【重要约束】本模块是唯一允许扫描 sysConfig.alarms/schedules 并重建下一唤醒点的地方；
App、BLE 和网络命令不得直接写 RTC 闹钟，也不得自行调用休眠调度器登记闹钟/日程。
*/
#pragma once

#include <Arduino.h>
#include <time.h>

enum class SysCalendarEventKind : uint8_t
{
    Schedule = 0,
    Alarm,
};

/** 调试/状态页可读取的只读摘要；不会暴露内部数组或触发 I2C。 */
struct SysCalendarStatus
{
    bool time_valid;
    bool next_event_valid;
    SysCalendarEventKind next_event_kind;
    time_t next_event_epoch;
    uint8_t occurrence_count;
    bool rtc_plan_programmed;
};

/** setup() 在配置和 SysTime 初始化完成后调用一次；只建立内存状态，不触发提醒。 */
void SysCalendar_Init();

/**
 * 主循环唯一维护入口：处理到期事件、合并数据/时间修订、重建触发表并按需写 RTC。
 * 同一分钟内先把所有到期日程入队，再处理闹钟，从代码结构上固定“日程 > 闹钟”的优先级。
 */
void SysCalendar_Update();

/**
 * Standby 在 RTC/ESP 定时器唤醒且外设仍休眠时调用：强制处理一次到期业务并重建 RTC 槽。
 * 返回 true 表示确有日程或闹钟到期，需要恢复前台；月初检查点等纯维护唤醒返回 false，可直接续睡。
 */
bool SysCalendar_ServiceSleepWake();

/**
 * 配置层在语言 profile 被保存或切换后调用，只递增数据代号，不扫描数组、不写 RTC。
 * 多次连续修改会由下一轮 SysCalendar_Update() 合并成一次重建。
 */
void SysCalendar_NotifyDataChanged();

/** 复制当前日历服务摘要；out_status 为空时返回 false。 */
bool SysCalendar_GetStatus(SysCalendarStatus *out_status);
