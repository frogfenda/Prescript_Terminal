/*
【模块职责】系统时间接口。

本项目的 UI 会在主菜单 HUD 中高频读取当前时分，因此时间读取必须是“立即返回”的：
不能在 HUD 绘制路径里调用带等待参数的 getLocalTime()，否则开机无网、NTP 未同步时，
每帧菜单动画都会被时间读取拖慢。

本模块提供五类能力：
1. 非阻塞读取当前时间，用于 HUD、闹钟、日程和时间设置页；
2. 开机从 PCF8563 恢复断电期间持续走时的年月日时分秒；
3. NTP 或手动设时成功后把系统时间写回 RTC；
4. 记录最近一次 NTP 成功对时发生在本次开机后的 millis，用于网络模块判断是否需要周期校时；
5. 把最近一次网络对时成功得到的 UTC epoch 保存到配置文件，作为 RTC 不可用时的开机兜底。

注意：保存的网络时间本身不会在断电期间继续计时，只用于 RTC 不可用时避开 1970/异常默认时间；
所以它不会被当成本次开机已经完成过 NTP，网络模块仍会按策略重新校时。
*/
#ifndef __SYS_TIME_H
#define __SYS_TIME_H

#include <Arduino.h>
#include <time.h>

/** 当前 ESP32 系统时间最近一次由哪个来源建立或校准。 */
enum class SysTimeSource : uint8_t
{
    Uncalibrated,
    Rtc,
    SavedNetwork,
    Network,
    Manual,
};

/**
 * 时间服务运行状态快照。
 * APP/UI 只能通过本结构了解 RTC 和时间来源，不应直接包含 BSP 头或访问 PCF8563 寄存器。
 */
struct SysTimeStatus
{
    SysTimeSource source;
    bool rtc_available;
    bool rtc_time_valid;
    bool rtc_write_attempted;
    bool rtc_last_write_ok;
};

/**
 * 初始化系统时间模块。
 *
 * 实现内容：
 * - 设置 POSIX 时区字符串 CST-8，对应 UTC+8；
 * - 清空本次开机的 NTP 对时记录；
 * - 优先初始化并读取 PCF8563，VL/BCD/日期校验通过后设置系统时间；
 * - RTC 不可用时，才使用配置中上次网络对时保存的可信 epoch；
 * - 不联网、不等待 NTP，不把 RTC/保存时间当成本次网络对时成功。
 *
 * 调用时机：
 * - main.cpp 的 setup() 中，在配置加载后、网络同步前调用一次。
 */
void SysTime_Init();

/**
 * 获取时间来源和 RTC 健康状态的立即快照；仅复制内存状态，不访问 I2C，也不会阻塞 UI。
 * 返回 false 仅表示 out_status 为空。
 */
bool SysTime_GetStatus(SysTimeStatus *out_status);

/**
 * 把当前 ESP32 系统本地时间写入 PCF8563。
 * 用于 NTP 成功、用户手动设时以及未来维护页面的显式校准；执行一次短 I2C 事务，不能在每帧调用。
 */
bool SysTime_SyncRtcFromSystem();

/**
 * 获取 HUD / 菜单显示用的 HH:MM 字符串。
 *
 * 实现方式：
 * - 使用 time(nullptr) 读取当前 epoch；
 * - 使用 localtime_r() 转为本地时区 tm；
 * - 格式化为 00:00 形式。
 *
 * 关键约束：
 * - 必须非阻塞；
 * - 不调用 getLocalTime(timeout)；
 * - 未联网或 NTP 失败时也立即返回本地计时。
 */
void SysTime_GetTimeString(char* out_str);

/**
 * 获取当前 tm 时间结构。
 *
 * 用途：
 * - 闹钟/日程后台检查当前年月日时分；
 * - 手动设置当日时间时读取当前日期；
 * - 其他业务需要拆分时间字段时使用。
 *
 * 返回：
 * - true：out_info 有效并已经写入当前本地时间；
 * - false：调用者传入空指针。
 */
bool SysTime_GetInfo(struct tm* out_info);

/**
 * 标记“网络 NTP 对时成功”。
 *
 * 网络任务完成 NTP 后调用此函数。
 * 这里不再次设置系统时间，因为 getLocalTime()/SNTP 已经完成了系统时间更新；
 * 本函数会记录本次开机内最近一次成功网络对时的 millis，用于后续计算校时间隔，
 * 并把当前 UTC epoch 保存到配置文件，供下次开机作为默认显示时间。
 */
void SysTime_MarkNetworkSynced();

/**
 * 返回距上次网络对时过去了多少毫秒。
 *
 * 返回值：
 * - UINT32_MAX：本次开机后从未完成过 NTP 对时；
 * - 其他值：millis() - 最近一次 NTP 成功时的 millis。
 *
 * 当前 UI 暂时不使用该接口，但保留给后续“时间状态/上次校时”显示。
 */
uint32_t SysTime_GetLastNetworkSyncAgeMs();

/**
 * 判断是否已经超过周期校时间隔。
 *
 * 网络模块在 Network_Update() 中调用它：
 * - 从未网络对时：返回 true；
 * - 已经网络对时，但距离上次对时超过 interval_ms：返回 true；
 * - 否则返回 false。
 *
 * 该函数只判断时间窗口，不负责检查 WiFi 是否配置、周期校时开关是否开启、网络任务是否忙。
 */
bool SysTime_ShouldPeriodicResync(uint32_t interval_ms);

/**
 * 手动设置“当日时分”。
 *
 * 实现方式：
 * - 读取当前本地日期；
 * - 拼出“当前日期 + 用户输入时分 + 00秒”；
 * - 手动按 UTC+8 换算成 UTC epoch；
 * - 通过 settimeofday() 写入系统时间。
 *
 * 这里刻意不使用 mktime()，避免 ESP32 Arduino 环境下手动设时出现时区偏移。
 *
 * 产品含义：
 * - 用户只修正当前时分，不在小屏上编辑年月日；
 * - 如果当前日期来自未联网的 1970 兜底日期，也只修正当天时分；
 * - 后续完整日期仍建议通过网络校时获得。
 */
void SysTime_SetTodayClock(uint8_t hour, uint8_t minute);

/**
 * 返回指定年月的最大天数。
 *
 * AppTimeDateSet 用它在用户旋钮修改年/月时实时限制“日”的范围，
 * 避免出现 2 月 31 日、4 月 31 日这类非法日期。
 */
uint8_t SysTime_DaysInMonth(uint16_t year, uint8_t month);

/**
 * 手动设置当前日期。
 *
 * 实现方式：
 * - 保留当前时分秒；
 * - 替换 year/month/day；
 * - 手动按 UTC+8 换算成 UTC epoch；
 * - 通过 settimeofday() 一次性写入系统时间。
 *
 * 这里同样不使用 mktime()，避免日期保存后出现额外时区偏移。
 *
 * 参数约定：
 * - year 使用完整年份，例如 2026；
 * - month 使用 1~12；
 * - day 会按月份自动钳制到合法范围。
 */
void SysTime_SetDate(uint16_t year, uint8_t month, uint8_t day);

#endif
