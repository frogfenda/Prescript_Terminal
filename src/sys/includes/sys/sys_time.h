/*
【模块职责】系统时间统一接口。

本模块只维护四条时间链路：
1. 开机、Light Sleep 唤醒和每小时维护时，将 PCF8563 时间同步到 ESP32；
2. 网络任务把真实 SNTP 结果排队，主循环再统一写入 ESP32 和 RTC；
3. 手动设置完整本地日期时间，并把同一结果同时写入 ESP32 和 RTC；
4. 为 HUD、闹钟和日程提供非阻塞时间读取与统一的本地时间转换。

RTC 寄存器访问仍由 BSP::Pcf8563 负责。本模块不把时间保存到配置文件，也不会在
网络任务中访问 I2C、LittleFS 或 UI。
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
    Network,
    Manual,
};

/** 调用 RTC→系统同步的原因，只用于状态记录和低频诊断。 */
enum class SysTimeRefreshReason : uint8_t
{
    Startup,
    Wakeup,
    Hourly,
};

/** 时间服务运行状态快照；读取该结构不会访问 I2C。 */
struct SysTimeStatus
{
    SysTimeSource source;
    bool rtc_available;
    bool rtc_time_valid;
    bool rtc_write_attempted;
    bool rtc_last_write_ok;
};

/**
 * 当前可信时间的一致快照。
 * epoch 与 local 来自同一次读取，revision 在网络、手动或 RTC 重新建立系统时间后递增，
 * 供日历调度器判断是否需要重建派生触发表。
 */
struct SysTimeSnapshot
{
    time_t epoch;
    struct tm local;
    uint32_t revision;
    bool valid;
};

/**
 * 初始化东八区规则、跨核心网络时间队列和 PCF8563。
 * setup() 在配置加载后调用一次；若 RTC 时间可信，会立即建立 ESP32 系统时间。
 */
void SysTime_Init();

/**
 * 主循环维护入口。
 * - 消费网络任务提交的真实 SNTP epoch，并写入 ESP32 与 RTC；
 * - 设备亮屏运行时，每满一小时从 RTC 重新同步系统时间。
 * 本函数必须从 Arduino 主循环调用，不得从网络/NFC 后台任务调用。
 */
void SysTime_Update();

/**
 * 从 PCF8563 读取本地日期时间并更新 ESP32 系统时间。
 * reason 用于区分开机、休眠唤醒和每小时维护；函数执行一次短 I2C 事务。
 * 返回 true 表示 RTC 数据和 settimeofday() 均成功。
 */
bool SysTime_RefreshFromRtc(SysTimeRefreshReason reason);

/**
 * 从后台网络任务提交真实 SNTP UTC epoch。
 * 本函数只写入长度为 1 的 FreeRTOS 队列，不访问 RTC、配置或 UI；较新的结果覆盖旧结果。
 * 返回 false 表示队列尚未建立或 epoch 明显无效。
 */
bool SysTime_SubmitNetworkTime(time_t epoch);

/**
 * 把当前 ESP32 本地时间写入 PCF8563。
 * 用于网络/手动校时完成后的统一持久化；执行一次短 I2C 事务，不能每帧调用。
 */
bool SysTime_SyncRtcFromSystem();

/** 获取时间服务状态的立即快照；out_status 为空时返回 false。 */
bool SysTime_GetStatus(SysTimeStatus *out_status);

/** 非阻塞获取 HUD 使用的 HH:MM 字符串。 */
void SysTime_GetTimeString(char *out_str);

/** 非阻塞读取当前本地 struct tm；out_info 为空时返回 false。 */
bool SysTime_GetInfo(struct tm *out_info);

/**
 * 读取当前 UTC epoch、东八区本地字段和时间修订号的一致快照；本函数不访问 I2C。
 * 返回 true 仅表示 out_snapshot 有效；时间是否已经由 RTC/网络/手动来源建立见 snapshot.valid。
 */
bool SysTime_GetSnapshot(SysTimeSnapshot *out_snapshot);

/** 返回当前 ESP32 UTC epoch；所有业务模块读取“现在”时应走本接口。 */
time_t SysTime_NowEpoch();

/** 当前时间修订号；只在系统时间被重新建立或校准后变化，0 保留为未初始化哨兵。 */
uint32_t SysTime_GetRevision();

/** 返回指定年月的实际天数；月份越界时钳制到 1～12。 */
uint8_t SysTime_DaysInMonth(uint16_t year, uint8_t month);

/**
 * 校验东八区本地年月日时分秒，并转换为 UTC epoch。
 * 支持 PCF8563 的 2000～2099 年范围；不会调用 mktime()，因此不会发生隐式日期归一化。
 */
bool SysTime_LocalDateTimeToEpoch(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    time_t *out_epoch
);

/**
 * 设置完整的东八区本地日期时间。
 * 校验成功后把同一个结果写入 ESP32，再立即写入 RTC；RTC 写入失败不会撤销本次系统时间。
 */
bool SysTime_SetLocalDateTime(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
);

/** 保留当前日期，只替换时分并通过统一完整时间接口写入系统和 RTC。 */
bool SysTime_SetTodayClock(uint8_t hour, uint8_t minute);

/** 保留当前时分秒，只替换年月日并通过统一完整时间接口写入系统和 RTC。 */
bool SysTime_SetDate(uint16_t year, uint8_t month, uint8_t day);

/** 返回距本次开机最近一次真实网络对时经过的毫秒；从未成功时返回 UINT32_MAX。 */
uint32_t SysTime_GetLastNetworkSyncAgeMs();

/** 判断真实网络对时是否已经超过指定间隔；本次开机从未成功时返回 true。 */
bool SysTime_ShouldPeriodicResync(uint32_t interval_ms);

#endif
