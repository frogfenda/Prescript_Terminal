/*
【模块职责】系统时间实现。

所有 RTC 读写和时间来源状态都在 Arduino 主任务中完成。网络任务只调用
SysTime_SubmitNetworkTime() 投递真实 SNTP epoch，避免跨核心访问 Wire1、配置文件和 UI。
PCF8563 是正常运行和断电重启后的常态时间源；网络与手动设置只负责校准系统并写回 RTC。
*/
#include "sys/sys_time.h"
#include "bsp/bsp_pcf8563.h"
#include "sys/sys_sleep_scheduler.h"
#include <sys/time.h>
#include <limits.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace
{
    constexpr int32_t LOCAL_UTC_OFFSET_SECONDS = 8 * 3600;
    constexpr uint32_t RTC_REFRESH_INTERVAL_MS = 60UL * 60UL * 1000UL;
    constexpr uint16_t MANUAL_TIME_MIN_YEAR = 2020;
    constexpr uint16_t MANUAL_TIME_MAX_YEAR = 2099;
    constexpr uint16_t MANUAL_TIME_FALLBACK_YEAR = 2026;
    constexpr uint8_t MANUAL_TIME_FALLBACK_MONTH = 1;
    constexpr uint8_t MANUAL_TIME_FALLBACK_DAY = 1;
    constexpr time_t NETWORK_TIME_MIN_EPOCH = 1577836800; // 2020-01-01 00:00:00 UTC
    constexpr time_t NETWORK_TIME_MAX_EPOCH = 4102444799; // 2099-12-31 23:59:59 UTC

    uint32_t s_last_network_sync_millis = 0;
    uint32_t s_last_rtc_refresh_millis = 0;
    bool s_has_network_sync = false;
    bool s_rtc_write_failed = false;
    QueueHandle_t s_network_time_queue = nullptr;

    /** 每次 RTC 读写维护完成后重新登记下一次静默唤醒；失败同样推迟一小时，避免离线时频繁唤醒。 */
    void ScheduleNextRtcMaintenance()
    {
        SysSleep_ScheduleAfterMs(
            SysSleepSource::RtcMaintenance,
            RTC_REFRESH_INTERVAL_MS,
            SysSleepWakeAction::SilentMaintenance);
    }

    SysTimeStatus s_time_status = {
        SysTimeSource::Uncalibrated,
        false,
        false,
        false,
        false,
    };

    /** 计算公历日期距离 1970-01-01 的天数，不依赖 C 库时区或 mktime()。 */
    int64_t DaysFromCivil(int year, unsigned month, unsigned day)
    {
        year -= month <= 2;
        const int era = (year >= 0 ? year : year - 399) / 400;
        const unsigned yoe = (unsigned)(year - era * 400);
        const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return (int64_t)era * 146097 + (int64_t)doe - 719468;
    }

    bool IsLeapYear(uint16_t year)
    {
        return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    }

    /** settimeofday() 只接收 UTC epoch，所有本地字段必须在调用前完成东八区换算。 */
    bool SetSystemEpoch(time_t epoch)
    {
        if (epoch < 0)
        {
            Serial.printf("[时间-错误] 拒绝写入负 epoch：%lld。\n", (long long)epoch);
            return false;
        }

        timeval tv = {};
        tv.tv_sec = epoch;
        int result = settimeofday(&tv, nullptr);
        if (result != 0)
        {
            Serial.printf("[时间-错误] 设置 ESP32 系统时间失败：epoch=%lld，错误码=%d。\n",
                          (long long)epoch,
                          result);
            return false;
        }
        return true;
    }

    /** 当前日期未校准时，为“只设置时分”提供不会产生负 epoch 的安全日期。 */
    void GetSafeDateForManualClock(uint16_t &year, uint8_t &month, uint8_t &day)
    {
        struct tm info = {};
        SysTime_GetInfo(&info);

        int current_year = info.tm_year + 1900;
        int current_month = info.tm_mon + 1;
        int current_day = info.tm_mday;
        bool valid =
            current_year >= MANUAL_TIME_MIN_YEAR && current_year <= MANUAL_TIME_MAX_YEAR &&
            current_month >= 1 && current_month <= 12 &&
            current_day >= 1 &&
            current_day <= SysTime_DaysInMonth((uint16_t)current_year, (uint8_t)current_month);

        if (valid)
        {
            year = (uint16_t)current_year;
            month = (uint8_t)current_month;
            day = (uint8_t)current_day;
            return;
        }

        year = MANUAL_TIME_FALLBACK_YEAR;
        month = MANUAL_TIME_FALLBACK_MONTH;
        day = MANUAL_TIME_FALLBACK_DAY;
        Serial.printf("[时间-警告] 当前日期不可信：%04d-%02d-%02d，手动设时使用安全日期 %04u-%02u-%02u。\n",
                      current_year,
                      current_month,
                      current_day,
                      year,
                      month,
                      day);
    }

    const char *RefreshReasonName(SysTimeRefreshReason reason)
    {
        switch (reason)
        {
        case SysTimeRefreshReason::Startup: return "开机";
        case SysTimeRefreshReason::Wakeup: return "休眠唤醒";
        case SysTimeRefreshReason::Hourly: return "每小时维护";
        default: return "未知";
        }
    }
}

uint8_t SysTime_DaysInMonth(uint16_t year, uint8_t month)
{
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    static const uint8_t DAYS_NORMAL[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && IsLeapYear(year))
        return 29;
    return DAYS_NORMAL[month - 1];
}

bool SysTime_LocalDateTimeToEpoch(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second,
    time_t *out_epoch)
{
    if (!out_epoch || year < 2000 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > SysTime_DaysInMonth(year, month) ||
        hour > 23 || minute > 59 || second > 59)
    {
        return false;
    }

    int64_t local_seconds =
        DaysFromCivil((int)year, (unsigned)month, (unsigned)day) * 86400LL +
        (int64_t)hour * 3600LL +
        (int64_t)minute * 60LL +
        (int64_t)second;

    *out_epoch = (time_t)(local_seconds - LOCAL_UTC_OFFSET_SECONDS);
    return *out_epoch >= 0;
}

void SysTime_Init()
{
    setenv("TZ", "CST-8", 1);
    tzset();

    s_last_network_sync_millis = 0;
    s_last_rtc_refresh_millis = millis();
    s_has_network_sync = false;
    s_rtc_write_failed = false;
    s_time_status = {SysTimeSource::Uncalibrated, false, false, false, false};

    if (!s_network_time_queue)
    {
        s_network_time_queue = xQueueCreate(1, sizeof(time_t));
        if (!s_network_time_queue)
            Serial.println("[时间-错误] 创建网络时间队列失败，NTP 结果将无法回送主循环。");
    }

    s_time_status.rtc_available = BSP::Pcf8563::Begin();
    if (s_time_status.rtc_available)
        SysTime_RefreshFromRtc(SysTimeRefreshReason::Startup);
    else
    {
        Serial.println("[时间-警告] RTC 初始化失败，等待网络或手动校时建立当前时间。");
        ScheduleNextRtcMaintenance();
    }
}

bool SysTime_RefreshFromRtc(SysTimeRefreshReason reason)
{
    /*
     * 网络/手动校时写 RTC 失败后，芯片里的旧时间已知不可信。
     * Light Sleep 内 ESP32 系统时钟仍会推进，因此先用当前正确系统时间修复 RTC，
     * 不能反向读取旧值覆盖刚得到的网络/手动时间。
     */
    if (s_rtc_write_failed)
        return SysTime_SyncRtcFromSystem();

    /* RTC 曾离线时先重新初始化；所有调用均在主任务，避免与网络线程交叉访问 Wire1。 */
    if (!BSP::Pcf8563::IsReady())
        s_time_status.rtc_available = BSP::Pcf8563::Begin();

    struct tm rtc_info = {};
    BSP::Pcf8563::TimeReadResult result = BSP::Pcf8563::ReadTime(&rtc_info);
    if (result != BSP::Pcf8563::TimeReadResult::Ok)
    {
        /* 失败也算一次维护尝试，避免 RTC 离线时静默休眠每 100ms 反复唤醒探测。 */
        s_last_rtc_refresh_millis = millis();
        s_time_status.rtc_time_valid = false;
        if (result == BSP::Pcf8563::TimeReadResult::BusError ||
            result == BSP::Pcf8563::TimeReadResult::NotInitialized)
            s_time_status.rtc_available = false;

        if (result == BSP::Pcf8563::TimeReadResult::VoltageLow)
            Serial.println("[时间-警告] RTC 的 VL 低压标志已置位，本次不采用 RTC 时间。");
        else if (result == BSP::Pcf8563::TimeReadResult::InvalidData)
            Serial.println("[时间-警告] RTC 日期时间非法，本次不采用 RTC 时间。");
        else
            Serial.println("[时间-错误] 读取 RTC 失败，保留 ESP32 当前系统时间。");
        ScheduleNextRtcMaintenance();
        return false;
    }

    time_t rtc_epoch = 0;
    if (!SysTime_LocalDateTimeToEpoch(
            (uint16_t)(rtc_info.tm_year + 1900),
            (uint8_t)(rtc_info.tm_mon + 1),
            (uint8_t)rtc_info.tm_mday,
            (uint8_t)rtc_info.tm_hour,
            (uint8_t)rtc_info.tm_min,
            (uint8_t)rtc_info.tm_sec,
            &rtc_epoch) ||
        !SetSystemEpoch(rtc_epoch))
    {
        s_last_rtc_refresh_millis = millis();
        s_time_status.rtc_time_valid = false;
        ScheduleNextRtcMaintenance();
        return false;
    }

    s_time_status.source = SysTimeSource::Rtc;
    s_time_status.rtc_available = true;
    s_time_status.rtc_time_valid = true;
    s_last_rtc_refresh_millis = millis();
    ScheduleNextRtcMaintenance();

    /* 每小时维护属于正常低频动作，不输出成功日志；开机和唤醒保留一条可追踪记录。 */
    if (reason != SysTimeRefreshReason::Hourly)
    {
        Serial.printf("[时间] %s已从 RTC 同步：%04d-%02d-%02d %02d:%02d:%02d。\n",
                      RefreshReasonName(reason),
                      rtc_info.tm_year + 1900,
                      rtc_info.tm_mon + 1,
                      rtc_info.tm_mday,
                      rtc_info.tm_hour,
                      rtc_info.tm_min,
                      rtc_info.tm_sec);
    }
    return true;
}

bool SysTime_SubmitNetworkTime(time_t epoch)
{
    if (!s_network_time_queue || epoch < NETWORK_TIME_MIN_EPOCH || epoch > NETWORK_TIME_MAX_EPOCH)
        return false;
    return xQueueOverwrite(s_network_time_queue, &epoch) == pdPASS;
}

void SysTime_Update()
{
    time_t network_epoch = 0;
    if (s_network_time_queue && xQueueReceive(s_network_time_queue, &network_epoch, 0) == pdTRUE)
    {
        if (SetSystemEpoch(network_epoch))
        {
            s_has_network_sync = true;
            s_last_network_sync_millis = millis();
            s_time_status.source = SysTimeSource::Network;
            SysTime_SyncRtcFromSystem();
            s_last_rtc_refresh_millis = millis();
            Serial.println("[时间] 已应用真实 NTP 时间并写入 RTC。");
        }
    }

    if ((uint32_t)(millis() - s_last_rtc_refresh_millis) >= RTC_REFRESH_INTERVAL_MS)
        SysTime_RefreshFromRtc(SysTimeRefreshReason::Hourly);
}

bool SysTime_SyncRtcFromSystem()
{
    if (!BSP::Pcf8563::IsReady())
        s_time_status.rtc_available = BSP::Pcf8563::Begin();

    time_t now = time(nullptr);
    struct tm local_info = {};
    localtime_r(&now, &local_info);

    s_time_status.rtc_write_attempted = true;
    s_time_status.rtc_last_write_ok = BSP::Pcf8563::WriteTime(local_info);
    if (!s_time_status.rtc_last_write_ok)
    {
        s_last_rtc_refresh_millis = millis();
        s_rtc_write_failed = true;
        s_time_status.rtc_available = BSP::Pcf8563::IsPresent();
        s_time_status.rtc_time_valid = false;
        Serial.println("[时间-警告] 当前时间写入 RTC 失败，ESP32 本次运行时间仍然有效。");
        ScheduleNextRtcMaintenance();
        return false;
    }

    s_time_status.rtc_available = true;
    s_time_status.rtc_time_valid = true;
    s_rtc_write_failed = false;
    s_last_rtc_refresh_millis = millis();
    ScheduleNextRtcMaintenance();
    return true;
}

bool SysTime_SetLocalDateTime(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second)
{
    time_t epoch = 0;
    if (!SysTime_LocalDateTimeToEpoch(year, month, day, hour, minute, second, &epoch))
    {
        Serial.printf("[时间-错误] 拒绝非法手动时间：%04u-%02u-%02u %02u:%02u:%02u。\n",
                      year, month, day, hour, minute, second);
        return false;
    }

    if (!SetSystemEpoch(epoch))
        return false;

    s_time_status.source = SysTimeSource::Manual;
    bool rtc_ok = SysTime_SyncRtcFromSystem();
    Serial.printf("[时间] 已手动设置完整时间：%04u-%02u-%02u %02u:%02u:%02u，RTC=%s。\n",
                  year, month, day, hour, minute, second, rtc_ok ? "成功" : "失败");
    return true;
}

bool SysTime_SetTodayClock(uint8_t hour, uint8_t minute)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    GetSafeDateForManualClock(year, month, day);
    return SysTime_SetLocalDateTime(year, month, day, hour, minute, 0);
}

bool SysTime_SetDate(uint16_t year, uint8_t month, uint8_t day)
{
    struct tm current = {};
    SysTime_GetInfo(&current);
    return SysTime_SetLocalDateTime(
        year,
        month,
        day,
        (uint8_t)current.tm_hour,
        (uint8_t)current.tm_min,
        (uint8_t)current.tm_sec);
}

bool SysTime_GetStatus(SysTimeStatus *out_status)
{
    if (!out_status)
        return false;
    *out_status = s_time_status;
    return true;
}

void SysTime_GetTimeString(char *out_str)
{
    if (!out_str)
        return;
    time_t now = time(nullptr);
    struct tm info = {};
    localtime_r(&now, &info);
    snprintf(out_str, 10, "%02d:%02d", info.tm_hour, info.tm_min);
}

bool SysTime_GetInfo(struct tm *out_info)
{
    if (!out_info)
        return false;
    time_t now = time(nullptr);
    localtime_r(&now, out_info);
    return true;
}

uint32_t SysTime_GetLastNetworkSyncAgeMs()
{
    return s_has_network_sync ? (uint32_t)(millis() - s_last_network_sync_millis) : UINT32_MAX;
}

bool SysTime_ShouldPeriodicResync(uint32_t interval_ms)
{
    return !s_has_network_sync || (uint32_t)(millis() - s_last_network_sync_millis) >= interval_ms;
}
