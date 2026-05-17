/*
【模块职责】系统时间实现。

这里把“显示时间”和“网络对时记录”分开处理：
- 显示时间直接读 time()/localtime_r()，保证 UI 不阻塞；
- 网络对时记录只保存最近一次 NTP 成功时的 millis，用于周期校时判断；
- 手动设置时间使用 settimeofday() 写入 ESP32 系统时间。
*/
#include "sys_time.h"
#include <sys/time.h>
#include <limits.h>

/* 最近一次 NTP 成功对时发生在本次开机后的 millis。 */
static uint32_t s_last_network_sync_millis = 0;

/* 本次开机是否已经完成过至少一次 NTP 对时。 */
static bool s_has_network_sync = false;


/*
 * 本设备固定使用东八区。
 *
 * 这里不再依赖 mktime() 把本地时间转 epoch，而是自己把“本地年月日时分秒”
 * 转换成 UTC epoch 后写入 settimeofday()。
 *
 * 这样做的原因：
 * - 用户在“设置当日时间”中输入 14:30，期望 HUD 立即显示 14:30；
 * - mktime() 会根据 C 库时区规则再次解释 tm 结构，在 ESP32 Arduino 环境里
 *   可能表现为额外偏移，看起来像“在当前时间上加了偏置”；
 * - 本项目当前只使用东八区，因此直接按 UTC+8 换算更稳定、可预期。
 */
static constexpr int32_t LOCAL_UTC_OFFSET_SECONDS = 8 * 3600;

/*
 * 手动设置时间时使用的安全日期范围。
 *
 * ESP32 刚开机且尚未 NTP 对时/日期设置时，系统时间通常仍在 1970 年附近。
 * 如果此时用户把当天时间设成 00:00，本地时间换算成 UTC 后会落到
 * 1969-12-31 16:00，也就是负 epoch。部分 ESP32/newlib 组合对负 epoch
 * 的 settimeofday()/localtime_r() 支持不稳定，会表现为：
 * - 00:00 设置失败；
 * - 一旦落入 1969/1970 异常日期，后续再设置其他时分也继续失效。
 *
 * 因此“只设置时分”的页面在发现当前日期不可信时，会先把日期钉到
 * 一个项目允许范围内的安全日期，再写入用户输入的时分。
 */
static constexpr uint16_t MANUAL_TIME_MIN_YEAR = 2020;
static constexpr uint16_t MANUAL_TIME_MAX_YEAR = 2035;
static constexpr uint16_t MANUAL_TIME_FALLBACK_YEAR = 2026;
static constexpr uint8_t  MANUAL_TIME_FALLBACK_MONTH = 1;
static constexpr uint8_t  MANUAL_TIME_FALLBACK_DAY = 1;

/**
 * 计算公历日期距离 1970-01-01 的天数。
 *
 * 输入：正常年月日，例如 2026-05-10。
 * 输出：该日期 00:00 相对 Unix epoch 日期的天数。
 *
 * 该算法不依赖 mktime() 和系统时区，因此适合作为手动设时的底层转换。
 */
static int64_t _DaysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;

    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/**
 * 把“东八区本地时间”转换成 settimeofday() 需要的 UTC epoch。
 *
 * 换算过程：
 * 1. 先计算本地日期对应的天数；
 * 2. 加上当天的时、分、秒；
 * 3. 减去东八区偏移 8 小时，得到 UTC 秒数。
 */
static time_t _EpochFromLocalDateTime(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
)
{
    int64_t days = _DaysFromCivil((int)year, (unsigned)month, (unsigned)day);

    int64_t local_seconds =
        days * 86400LL +
        (int64_t)hour * 3600LL +
        (int64_t)minute * 60LL +
        (int64_t)second;

    int64_t utc_seconds = local_seconds - LOCAL_UTC_OFFSET_SECONDS;
    return (time_t)utc_seconds;
}

/**
 * 统一写入 ESP32 系统时间。
 *
 * settimeofday() 接收的是 UTC epoch，不是本地时间。
 * 所以调用本函数前必须已经完成本地时间到 UTC epoch 的换算。
 */
static bool _SetSystemEpoch(time_t epoch)
{
    if (epoch < 0)
    {
        Serial.printf("[时间-错误] 拒绝写入负 epoch：%lld。\n", (long long)epoch);
        return false;
    }

    timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;

    int ret = settimeofday(&tv, nullptr);
    if (ret != 0)
    {
        Serial.printf("[时间-错误] settimeofday 失败，epoch=%lld，ret=%d。\n", (long long)epoch, ret);
        return false;
    }

    return true;
}


/**
 * 判断闰年。
 * 公历规则：能被 4 整除且不能被 100 整除，或者能被 400 整除。
 */
static bool _IsLeapYear(uint16_t year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t SysTime_DaysInMonth(uint16_t year, uint8_t month)
{
    if (month < 1) month = 1;
    if (month > 12) month = 12;

    static const uint8_t days_normal[] = {31,28,31,30,31,30,31,31,30,31,30,31};

    if (month == 2 && _IsLeapYear(year))
        return 29;

    return days_normal[month - 1];
}

/**
 * 从当前系统时间中取出一个可用于“手动设置时分”的安全本地日期。
 *
 * 正常情况下直接使用当前年月日；如果系统还停留在 1970/1969 等
 * 未校准状态，则回退到固定安全日期，避免 00:00 被换算成负 epoch。
 */
static void _GetSafeDateForManualClock(uint16_t &year, uint8_t &month, uint8_t &day)
{
    struct tm info;
    SysTime_GetInfo(&info);

    int current_year = info.tm_year + 1900;
    int current_month = info.tm_mon + 1;
    int current_day = info.tm_mday;

    bool date_ok =
        current_year >= MANUAL_TIME_MIN_YEAR &&
        current_year <= MANUAL_TIME_MAX_YEAR &&
        current_month >= 1 && current_month <= 12 &&
        current_day >= 1 && current_day <= SysTime_DaysInMonth((uint16_t)current_year, (uint8_t)current_month);

    if (!date_ok)
    {
        year = MANUAL_TIME_FALLBACK_YEAR;
        month = MANUAL_TIME_FALLBACK_MONTH;
        day = MANUAL_TIME_FALLBACK_DAY;

        Serial.printf(
            "[时间-警告] 当前系统日期不可信：%04d-%02d-%02d，手动设时改用安全日期 %04u-%02u-%02u。\n",
            current_year,
            current_month,
            current_day,
            year,
            month,
            day
        );
        return;
    }

    year = (uint16_t)current_year;
    month = (uint8_t)current_month;
    day = (uint8_t)current_day;
}

void SysTime_Init()
{
    /*
     * POSIX TZ 写法中 CST-8 表示 UTC+8。
     * 这里设置的是“epoch 转本地时间”的规则；
     * 不代表已经完成 NTP 对时。
     */
    setenv("TZ", "CST-8", 1);
    tzset();

    s_last_network_sync_millis = 0;
    s_has_network_sync = false;
}

void SysTime_GetTimeString(char* out_str)
{
    if (!out_str)
        return;

    /*
     * 不能使用 getLocalTime(&tm, timeout)。
     * 主菜单滚轮每帧都会间接绘制 HUD，若 NTP 未同步且这里等待 10ms，
     * 16ms 一帧的菜单动画会被拖成明显卡顿。
     */
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    snprintf(out_str, 10, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

bool SysTime_GetInfo(struct tm* out_info)
{
    if (!out_info)
        return false;

    time_t now = time(nullptr);
    localtime_r(&now, out_info);
    return true;
}

void SysTime_MarkNetworkSynced()
{
    s_has_network_sync = true;
    s_last_network_sync_millis = millis();

    Serial.printf(
        "[时间] NTP 对时完成，周期校时计时点重置为 %lu ms。\n",
        (unsigned long)s_last_network_sync_millis
    );
}

uint32_t SysTime_GetLastNetworkSyncAgeMs()
{
    if (!s_has_network_sync)
        return UINT32_MAX;

    return millis() - s_last_network_sync_millis;
}

bool SysTime_ShouldPeriodicResync(uint32_t interval_ms)
{
    if (!s_has_network_sync)
        return true;

    return (millis() - s_last_network_sync_millis) >= interval_ms;
}

void SysTime_SetTodayClock(uint8_t hour, uint8_t minute)
{
    if (hour > 23)
        hour = 23;

    if (minute > 59)
        minute = 59;

    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    _GetSafeDateForManualClock(year, month, day);

    /*
     * 直接设置“当前本地日期 + 用户输入的时分”。
     *
     * 这里不是：
     * - 在当前 epoch 上加减偏置；
     * - 把 hour/minute 当成相对偏移；
     * - 交给 mktime() 再次解释时区。
     *
     * 而是直接拼出目标本地时间，再按 UTC+8 换算成 UTC epoch 写入系统。
     * 因此用户保存 14:30 后，HUD 应该立即显示 14:30。
     */
    time_t new_epoch = _EpochFromLocalDateTime(year, month, day, hour, minute, 0);
    if (!_SetSystemEpoch(new_epoch))
    {
        Serial.printf(
            "[时间-错误] 手动设置当日时间失败：%04u-%02u-%02u %02u:%02u:00。\n",
            year,
            month,
            day,
            hour,
            minute
        );
        return;
    }

    time_t now = time(nullptr);
    struct tm verify;
    localtime_r(&now, &verify);

    Serial.printf(
        "[时间] 已手动设置当日时间：%04u-%02u-%02u %02u:%02u:00，校验显示=%04d-%02d-%02d %02d:%02d。\n",
        year,
        month,
        day,
        hour,
        minute,
        verify.tm_year + 1900,
        verify.tm_mon + 1,
        verify.tm_mday,
        verify.tm_hour,
        verify.tm_min
    );
}

void SysTime_SetDate(uint16_t year, uint8_t month, uint8_t day)
{
    if (year < 2020) year = 2020;
    if (year > 2035) year = 2035;
    if (month < 1) month = 1;
    if (month > 12) month = 12;

    uint8_t max_day = SysTime_DaysInMonth(year, month);
    if (day < 1) day = 1;
    if (day > max_day) day = max_day;

    struct tm info;
    SysTime_GetInfo(&info);

    uint8_t hour = (uint8_t)info.tm_hour;
    uint8_t minute = (uint8_t)info.tm_min;
    uint8_t second = (uint8_t)info.tm_sec;

    /*
     * 直接设置“用户输入的年月日 + 当前本地时分秒”。
     *
     * 日期设置页只负责修正年月日，因此这里保留当前时分秒。
     * 同样不使用 mktime()，避免日期保存时被时区换算二次偏移。
     */
    time_t new_epoch = _EpochFromLocalDateTime(year, month, day, hour, minute, second);
    if (!_SetSystemEpoch(new_epoch))
    {
        Serial.printf(
            "[时间-错误] 手动设置日期失败：%04u-%02u-%02u。\n",
            year,
            month,
            day
        );
        return;
    }

    time_t now = time(nullptr);
    struct tm verify;
    localtime_r(&now, &verify);

    Serial.printf(
        "[时间] 已手动设置日期：%04u-%02u-%02u，保留时间=%02u:%02u:%02u，校验显示=%04d-%02d-%02d %02d:%02d:%02d。\n",
        year,
        month,
        day,
        hour,
        minute,
        second,
        verify.tm_year + 1900,
        verify.tm_mon + 1,
        verify.tm_mday,
        verify.tm_hour,
        verify.tm_min,
        verify.tm_sec
    );
}
