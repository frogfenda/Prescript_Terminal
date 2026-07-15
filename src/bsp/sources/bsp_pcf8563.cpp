/*
【模块职责】PCF8563 RTC 板级驱动实现。
【芯片依据】寄存器、位定义和读写时序按 NXP PCF8563 Rev.11 数据手册实现；
原理图料号标为 PCF8563MDTR(XBLW)，当前未取得 XBLW 厂商的独立差异手册，
因此兼容性必须通过上板读取 0x51、VL 标志和走时结果验证，不能只凭相似料号认定完全一致。
【并发约束】本项目在 Arduino 主任务中调用 RTC；Wire1 还与 TM6605 共用，单次事务必须短于手册要求的 1 秒。
*/
#include "bsp/bsp_pcf8563.h"
#include "bsp/bsp_pins.h"

namespace
{
    static constexpr uint8_t REG_CONTROL_STATUS_1 = 0x00;
    static constexpr uint8_t REG_CONTROL_STATUS_2 = 0x01;
    static constexpr uint8_t REG_SECONDS = 0x02;
    static constexpr uint8_t REG_MINUTE_ALARM = 0x09;
    static constexpr uint8_t REG_CLKOUT_CONTROL = 0x0D;
    static constexpr uint8_t REG_TIMER_CONTROL = 0x0E;

    static constexpr uint8_t CTRL2_TI_TP = 0x10;
    static constexpr uint8_t CTRL2_AF = 0x08;
    static constexpr uint8_t CTRL2_TF = 0x04;
    static constexpr uint8_t CTRL2_AIE = 0x02;
    static constexpr uint8_t CTRL2_TIE = 0x01;

    static constexpr uint8_t TIMER_DISABLED_LOW_POWER = 0x03;

    TwoWire *s_wire = &Wire1;
    uint8_t s_address = BSP::Pcf8563::DEFAULT_ADDRESS;
    bool s_initialized = false;
    bool s_ready = false;

    uint8_t ToBcd(uint8_t value)
    {
        return (uint8_t)(((value / 10) << 4) | (value % 10));
    }

    uint8_t FromBcd(uint8_t value)
    {
        return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
    }

    /** 检查掩码后的 BCD 两个半字节以及最终数值，避免把 0x1A 这类非法内容解码成合法整数。 */
    bool IsValidBcd(uint8_t raw, uint8_t mask, uint8_t minimum, uint8_t maximum)
    {
        uint8_t value = raw & mask;
        if ((value & 0x0F) > 9 || ((value >> 4) & 0x0F) > 9)
            return false;

        uint8_t decoded = FromBcd(value);
        return decoded >= minimum && decoded <= maximum;
    }

    bool IsLeapYear(uint16_t year)
    {
        return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    }

    uint8_t DaysInMonth(uint16_t year, uint8_t month)
    {
        static const uint8_t days_normal[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        if (month < 1 || month > 12)
            return 0;
        if (month == 2 && IsLeapYear(year))
            return 29;
        return days_normal[month - 1];
    }

    bool IsValidLocalTime(const struct tm &info)
    {
        int year = info.tm_year + 1900;
        int month = info.tm_mon + 1;

        return year >= 2000 && year <= 2099 &&
               month >= 1 && month <= 12 &&
               info.tm_mday >= 1 && info.tm_mday <= DaysInMonth((uint16_t)year, (uint8_t)month) &&
               info.tm_wday >= 0 && info.tm_wday <= 6 &&
               info.tm_hour >= 0 && info.tm_hour <= 23 &&
               info.tm_min >= 0 && info.tm_min <= 59 &&
               info.tm_sec >= 0 && info.tm_sec <= 59;
    }

    int64_t DaysFromCivil(int year, unsigned month, unsigned day)
    {
        year -= month <= 2;
        const int era = (year >= 0 ? year : year - 399) / 400;
        const unsigned yoe = (unsigned)(year - era * 400);
        const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return (int64_t)era * 146097 + (int64_t)doe - 719468;
    }

    uint8_t WeekdayFromDate(uint16_t year, uint8_t month, uint8_t day)
    {
        int weekday = (int)((DaysFromCivil((int)year, month, day) + 4) % 7);
        if (weekday < 0)
            weekday += 7;
        return (uint8_t)weekday;
    }

    bool ReadRegs(uint8_t reg, uint8_t *data, size_t len)
    {
        if (!s_wire || !data || len == 0)
            return false;

        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        if (s_wire->endTransmission(false) != 0)
            return false;

        size_t count = s_wire->requestFrom((uint16_t)s_address, len, true);
        if (count != len)
            return false;

        for (size_t i = 0; i < len; ++i)
            data[i] = (uint8_t)s_wire->read();
        return true;
    }

    bool WriteRegs(uint8_t reg, const uint8_t *data, size_t len)
    {
        if (!s_wire || !data || len == 0)
            return false;

        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        for (size_t i = 0; i < len; ++i)
            s_wire->write(data[i]);
        return s_wire->endTransmission() == 0;
    }

    bool ReadReg(uint8_t reg, uint8_t &value)
    {
        return ReadRegs(reg, &value, 1);
    }

    bool WriteReg(uint8_t reg, uint8_t value)
    {
        return WriteRegs(reg, &value, 1);
    }

    bool ConfigureLowPowerDefaults()
    {
        bool ok = true;

        // INT# 未接线且当前没有硬件倒计时用户：清空旧标志并禁用两类中断。
        ok &= WriteReg(REG_CONTROL_STATUS_2, 0x00);

        // CLKOUT 悬空，禁用后可把典型备用电流从约 0.55 uA 降到约 0.25 uA（VDD=3 V）。
        ok &= WriteReg(REG_CLKOUT_CONTROL, (uint8_t)BSP::Pcf8563::ClockOut::Disabled);

        // 手册要求未使用倒计时器时选择 1/60 Hz 源并保持 TE=0，以获得最低功耗。
        ok &= WriteReg(REG_TIMER_CONTROL, TIMER_DISABLED_LOW_POWER);
        return ok;
    }
}

namespace BSP::Pcf8563
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        s_wire = &wire;
        s_address = address == 0 ? DEFAULT_ADDRESS : address;
        s_initialized = false;
        s_ready = false;

        if (s_wire == &Wire1)
        {
            s_wire->begin(Pins::I2C_SDA, Pins::I2C_SCL);
            s_wire->setClock(100000);
            s_wire->setTimeOut(20);
        }

        s_initialized = true;
        if (!IsPresent())
        {
            Serial.println("[BSP][RTC] PCF8563 无响应，RTC 将保持离线。");
            return false;
        }

        bool ok = StartClock() && ConfigureLowPowerDefaults();
        s_ready = ok;
        if (!ok)
            Serial.println("[BSP][RTC] PCF8563 默认寄存器配置失败。");
        return ok;
    }

    bool IsReady()
    {
        return s_initialized && s_ready;
    }

    bool IsPresent()
    {
        if (!s_wire)
            return false;

        s_wire->beginTransmission(s_address);
        return s_wire->endTransmission() == 0;
    }

    TimeReadResult ReadTime(struct tm *out_info)
    {
        if (!out_info)
            return TimeReadResult::InvalidArgument;
        if (!s_initialized)
            return TimeReadResult::NotInitialized;

        uint8_t data[7] = {};
        if (!ReadRegs(REG_SECONDS, data, sizeof(data)))
        {
            s_ready = false;
            return TimeReadResult::BusError;
        }

        // VL=1 表示 VDD 曾低于数据保持阈值或振荡器停止，字段即使看似合法也不能作为可信时间。
        if ((data[0] & 0x80) != 0)
            return TimeReadResult::VoltageLow;

        bool bcd_ok =
            IsValidBcd(data[0], 0x7F, 0, 59) &&
            IsValidBcd(data[1], 0x7F, 0, 59) &&
            IsValidBcd(data[2], 0x3F, 0, 23) &&
            IsValidBcd(data[3], 0x3F, 1, 31) &&
            (data[4] & 0x07) <= 6 &&
            IsValidBcd(data[5], 0x1F, 1, 12) &&
            IsValidBcd(data[6], 0xFF, 0, 99);
        if (!bcd_ok)
            return TimeReadResult::InvalidData;

        struct tm info = {};
        info.tm_sec = FromBcd(data[0] & 0x7F);
        info.tm_min = FromBcd(data[1] & 0x7F);
        info.tm_hour = FromBcd(data[2] & 0x3F);
        info.tm_mday = FromBcd(data[3] & 0x3F);
        info.tm_wday = data[4] & 0x07;
        info.tm_mon = FromBcd(data[5] & 0x1F) - 1;

        // 产品允许范围是 2020～2035；PCF8563 的 C 位只在跨世纪时翻转，因此本项目固定解释为 20xx。
        info.tm_year = 2000 + FromBcd(data[6]) - 1900;
        info.tm_isdst = 0;

        if (!IsValidLocalTime(info))
            return TimeReadResult::InvalidData;

        *out_info = info;
        s_ready = true;
        return TimeReadResult::Ok;
    }

    bool WriteTime(const struct tm &info)
    {
        if (!s_initialized || !IsValidLocalTime(info))
            return false;

        uint16_t year = (uint16_t)(info.tm_year + 1900);
        uint8_t month = (uint8_t)(info.tm_mon + 1);
        uint8_t day = (uint8_t)info.tm_mday;
        uint8_t weekday = WeekdayFromDate(year, month, day);

        uint8_t data[7] = {
            (uint8_t)(ToBcd((uint8_t)info.tm_sec) & 0x7F),
            (uint8_t)(ToBcd((uint8_t)info.tm_min) & 0x7F),
            (uint8_t)(ToBcd((uint8_t)info.tm_hour) & 0x3F),
            (uint8_t)(ToBcd(day) & 0x3F),
            (uint8_t)(weekday & 0x07),
            (uint8_t)(ToBcd(month) & 0x1F),
            ToBcd((uint8_t)(year - 2000)),
        };

        bool ok = WriteRegs(REG_SECONDS, data, sizeof(data));
        s_ready = ok;
        return ok;
    }

    bool StartClock()
    {
        if (!s_initialized)
            return false;

        // 正常模式要求 TEST1、STOP、TESTC 和所有未使用位都写 0。
        bool ok = WriteReg(REG_CONTROL_STATUS_1, 0x00);
        s_ready = ok;
        return ok;
    }

    bool ConfigureAlarm(const struct tm &info)
    {
        if (!s_initialized || !IsValidLocalTime(info))
            return false;

        uint8_t data[4] = {
            (uint8_t)(ToBcd((uint8_t)info.tm_min) & 0x7F),
            (uint8_t)(ToBcd((uint8_t)info.tm_hour) & 0x3F),
            (uint8_t)(ToBcd((uint8_t)info.tm_mday) & 0x3F),
            0x80,
        };
        if (!WriteRegs(REG_MINUTE_ALARM, data, sizeof(data)))
        {
            s_ready = false;
            return false;
        }

        uint8_t status2 = 0;
        if (!ReadReg(REG_CONTROL_STATUS_2, status2))
        {
            s_ready = false;
            return false;
        }

        /*
         * 写 AF=0 清除旧闹钟标志；TF 必须按读值写回，避免配置闹钟时误清倒计时器事件。
         * 未使用高位必须写 0，只保留 TI_TP、TF、TIE 三个与倒计时器相关的有效位。
         */
        uint8_t next_status = status2 & (CTRL2_TI_TP | CTRL2_TF | CTRL2_TIE);
        next_status |= CTRL2_AIE;
        bool ok = WriteReg(REG_CONTROL_STATUS_2, next_status);
        s_ready = ok;
        return ok;
    }

    bool ClearAlarm()
    {
        if (!s_initialized)
            return false;

        uint8_t disabled_alarm[4] = {0x80, 0x80, 0x80, 0x80};
        if (!WriteRegs(REG_MINUTE_ALARM, disabled_alarm, sizeof(disabled_alarm)))
        {
            s_ready = false;
            return false;
        }

        uint8_t status2 = 0;
        if (!ReadReg(REG_CONTROL_STATUS_2, status2))
        {
            s_ready = false;
            return false;
        }

        // AIE=0 禁用闹钟中断、AF=0 清标志；保留倒计时器模式、TF 和 TIE。
        uint8_t next_status = status2 & (CTRL2_TI_TP | CTRL2_TF | CTRL2_TIE);
        bool ok = WriteReg(REG_CONTROL_STATUS_2, next_status);
        s_ready = ok;
        return ok;
    }

    bool ConfigureClockOutput(ClockOut output)
    {
        if (!s_initialized)
            return false;

        bool ok = WriteReg(REG_CLKOUT_CONTROL, (uint8_t)output);
        s_ready = ok;
        return ok;
    }
}
