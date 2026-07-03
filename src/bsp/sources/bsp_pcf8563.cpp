/*
【模块职责】PCF8563 RTC 板级驱动实现。这里直接接触 Wire/I2C、PCF8563 寄存器和 BCD 日期时间格式。
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

    static constexpr uint8_t CTRL2_AIE = 0x02;
    static constexpr uint8_t CTRL2_TF = 0x04;
    static constexpr uint8_t CTRL2_AF = 0x08;

    TwoWire *s_wire = &Wire1;
    uint8_t s_address = BSP::Pcf8563::DEFAULT_ADDRESS;
    bool s_ready = false;

    uint8_t ToBcd(uint8_t value)
    {
        return (uint8_t)(((value / 10) << 4) | (value % 10));
    }

    uint8_t FromBcd(uint8_t value)
    {
        return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
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
        int day = info.tm_mday;

        if (year < 2000 || year > 2099)
            return false;
        if (month < 1 || month > 12)
            return false;
        if (day < 1 || day > DaysInMonth((uint16_t)year, (uint8_t)month))
            return false;
        if (info.tm_hour < 0 || info.tm_hour > 23)
            return false;
        if (info.tm_min < 0 || info.tm_min > 59)
            return false;
        if (info.tm_sec < 0 || info.tm_sec > 59)
            return false;

        return true;
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
        int64_t days = DaysFromCivil((int)year, (unsigned)month, (unsigned)day);
        int weekday = (int)((days + 4) % 7);
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
            data[i] = s_wire->read();

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

    bool ClearInterruptState()
    {
        return WriteReg(REG_CONTROL_STATUS_2, 0x00);
    }
}

namespace BSP::Pcf8563
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        s_wire = &wire;
        s_address = address;

        if (s_wire == &Wire1)
        {
            s_wire->begin(Pins::I2C_SDA, Pins::I2C_SCL);
            s_wire->setTimeOut(20);
        }

        if (!IsPresent())
        {
            s_ready = false;
            Serial.println("[BSP][RTC] PCF8563 无响应，本轮初始化失败。");
            return false;
        }

        bool ok = true;
        ok &= StartClock();
        ok &= ClearInterruptState();
        ok &= ConfigureClockOutput(ClockOut::Hz1);

        s_ready = ok;
        Serial.println(ok ? "[BSP][RTC] PCF8563 初始化完成。" : "[BSP][RTC] PCF8563 初始化失败。");
        return ok;
    }

    bool IsReady()
    {
        return s_ready;
    }

    bool IsPresent()
    {
        if (!s_wire)
            return false;

        s_wire->beginTransmission(s_address);
        return s_wire->endTransmission() == 0;
    }

    bool ReadTime(struct tm *out_info)
    {
        if (!out_info)
            return false;

        uint8_t data[7] = {};
        if (!ReadRegs(REG_SECONDS, data, sizeof(data)))
        {
            s_ready = false;
            return false;
        }

        struct tm info = {};
        info.tm_sec = FromBcd(data[0] & 0x7F);
        info.tm_min = FromBcd(data[1] & 0x7F);
        info.tm_hour = FromBcd(data[2] & 0x3F);
        info.tm_mday = FromBcd(data[3] & 0x3F);
        info.tm_wday = data[4] & 0x07;
        info.tm_mon = FromBcd(data[5] & 0x1F) - 1;
        info.tm_year = 2000 + FromBcd(data[6]) - 1900;
        info.tm_isdst = 0;

        bool has_valid_oscillator = (data[0] & 0x80) == 0;
        if (!has_valid_oscillator || !IsValidLocalTime(info))
            return false;

        *out_info = info;
        return true;
    }

    bool WriteTime(const struct tm &info)
    {
        if (!IsValidLocalTime(info))
            return false;

        uint16_t year = (uint16_t)(info.tm_year + 1900);
        uint8_t month = (uint8_t)(info.tm_mon + 1);
        uint8_t day = (uint8_t)info.tm_mday;
        uint8_t weekday = (info.tm_wday >= 0 && info.tm_wday <= 6)
            ? (uint8_t)info.tm_wday
            : WeekdayFromDate(year, month, day);

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
        if (!ok)
            s_ready = false;
        return ok;
    }

    bool StartClock()
    {
        bool ok = WriteReg(REG_CONTROL_STATUS_1, 0x00);
        if (!ok)
            s_ready = false;
        return ok;
    }

    bool ConfigureAlarm(const struct tm &info)
    {
        if (!IsValidLocalTime(info))
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

        status2 &= (uint8_t)~(CTRL2_AF | CTRL2_TF);
        status2 |= CTRL2_AIE;
        bool ok = WriteReg(REG_CONTROL_STATUS_2, status2);
        if (!ok)
            s_ready = false;
        return ok;
    }

    bool ClearAlarm()
    {
        uint8_t data[4] = {0x80, 0x80, 0x80, 0x80};
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

        status2 &= (uint8_t)~(CTRL2_AIE | CTRL2_AF | CTRL2_TF);
        bool ok = WriteReg(REG_CONTROL_STATUS_2, status2);
        if (!ok)
            s_ready = false;
        return ok;
    }

    bool ConfigureClockOutput(ClockOut output)
    {
        bool ok = WriteReg(REG_CLKOUT_CONTROL, (uint8_t)output);
        if (!ok)
            s_ready = false;
        return ok;
    }
}
