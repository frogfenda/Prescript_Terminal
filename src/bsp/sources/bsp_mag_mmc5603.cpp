/*
【模块职责】实现MMC5603NJ共享I2C驱动，寄存器和换算依据MEMSIC MMC5603NJ Datasheet Rev.B。
【错误策略】高频Read不重试、不重建Wire1；事务失败后由SysMag按低频恢复。芯片控制寄存器
0x1A～0x1D为只写，因此初始化只以产品ID、OTP完成位和全部写事务应答作为配置成立条件。
*/
#include "bsp/bsp_mag_mmc5603.h"

namespace
{
    static constexpr uint8_t REG_DATA = 0x00;
    static constexpr uint8_t REG_STATUS1 = 0x18;
    static constexpr uint8_t REG_ODR = 0x1A;
    static constexpr uint8_t REG_CTRL0 = 0x1B;
    static constexpr uint8_t REG_CTRL1 = 0x1C;
    static constexpr uint8_t REG_CTRL2 = 0x1D;
    static constexpr uint8_t REG_PRODUCT_ID = 0x39;

    static constexpr uint8_t STATUS_T_DONE = 0x80;
    static constexpr uint8_t STATUS_M_DONE = 0x40;
    static constexpr uint8_t STATUS_SELF_TEST_SIGNAL = 0x20;
    static constexpr uint8_t STATUS_OTP_DONE = 0x10;
    static constexpr uint8_t CTRL0_CMM_FREQ_EN = 0x80;
    static constexpr uint8_t CTRL0_AUTO_SR_EN = 0x20;
    static constexpr uint8_t CTRL0_DO_RESET = 0x10;
    static constexpr uint8_t CTRL0_DO_SET = 0x08;
    static constexpr uint8_t CTRL1_SW_RESET = 0x80;
    static constexpr uint8_t CTRL2_CMM_EN = 0x10;
    static constexpr uint8_t CTRL2_PERIODIC_SET_EN = 0x08;

    static constexpr int32_t RAW_ZERO_FIELD = 524288;
    static constexpr float COUNTS_PER_UT = 163.84f;
    static constexpr uint32_t RESET_DELAY_MS = 20;
    static constexpr uint32_t OTP_TIMEOUT_MS = 50;

    TwoWire *s_wire = &Wire1;
    uint8_t s_address = 0;
    BSP::Mmc5603::Config s_config = {};
    BSP::Mmc5603::Error s_last_error = BSP::Mmc5603::Error::NotInitialized;
    BSP::Mmc5603::Diagnostics s_diagnostics = {};
    bool s_initialized = false;
    bool s_ready = false;
    bool s_powered_down = true;

    bool Fail(BSP::Mmc5603::Error error, bool mark_not_ready)
    {
        s_last_error = error;
        if (mark_not_ready)
            s_ready = false;
        return false;
    }

    void ClearError()
    {
        s_last_error = BSP::Mmc5603::Error::None;
    }

    bool AckAt(uint8_t address)
    {
        if (!s_wire || address == 0)
            return false;
        s_wire->beginTransmission(address);
        return s_wire->endTransmission() == 0;
    }

    bool ReadRegsAt(uint8_t address, uint8_t reg, uint8_t *data, size_t length)
    {
        if (!s_wire || address == 0 || !data || length == 0)
            return false;
        s_wire->beginTransmission(address);
        s_wire->write(reg);
        if (s_wire->endTransmission(false) != 0)
            return false;
        const size_t received = s_wire->requestFrom(static_cast<uint16_t>(address), length, true);
        if (received != length)
        {
            while (s_wire->available())
                s_wire->read();
            return false;
        }
        for (size_t index = 0; index < length; ++index)
            data[index] = static_cast<uint8_t>(s_wire->read());
        return true;
    }

    bool ReadReg(uint8_t reg, uint8_t &value)
    {
        return ReadRegsAt(s_address, reg, &value, 1);
    }

    bool WriteReg(uint8_t reg, uint8_t value)
    {
        if (!s_wire || s_address == 0)
            return false;
        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        s_wire->write(value);
        return s_wire->endTransmission() == 0;
    }

    bool ProbeAddress(uint8_t address)
    {
        s_diagnostics.requestedAddress = address;
        s_diagnostics.addressAcknowledged = AckAt(address);
        if (!s_diagnostics.addressAcknowledged)
            return false;
        uint8_t product_id = 0;
        s_diagnostics.productIdValid = ReadRegsAt(address, REG_PRODUCT_ID, &product_id, 1);
        s_diagnostics.productId = product_id;
        if (!s_diagnostics.productIdValid || product_id != BSP::Mmc5603::PRODUCT_ID_VALUE)
            return false;
        s_diagnostics.detectedAddress = address;
        return true;
    }

    bool ConfigValid(const BSP::Mmc5603::Config &config)
    {
        if (config.outputRateHz == 0)
            return false;
        switch (config.bandwidth)
        {
        case BSP::Mmc5603::Bandwidth::Ms6_6:
            return config.outputRateHz <= 75;
        case BSP::Mmc5603::Bandwidth::Ms3_5:
            return config.outputRateHz <= 150;
        case BSP::Mmc5603::Bandwidth::Ms2_0:
        case BSP::Mmc5603::Bandwidth::Ms1_2:
            return true;
        default:
            return false;
        }
    }

    bool WaitForOtp()
    {
        const uint32_t started_ms = millis();
        do
        {
            uint8_t status = 0;
            if (!ReadReg(REG_STATUS1, status))
                return Fail(BSP::Mmc5603::Error::BusError, true);
            s_diagnostics.statusValid = true;
            s_diagnostics.status = status;
            s_diagnostics.otpLoaded = (status & STATUS_OTP_DONE) != 0;
            if (s_diagnostics.otpLoaded)
                return true;
            delay(1);
        } while (millis() - started_ms < OTP_TIMEOUT_MS);
        return Fail(BSP::Mmc5603::Error::OtpNotReady, true);
    }

    int32_t DecodeAxis(uint8_t high, uint8_t middle, uint8_t low)
    {
        const uint32_t raw20 = (static_cast<uint32_t>(high) << 12) |
                               (static_cast<uint32_t>(middle) << 4) |
                               (low & 0x0F);
        return static_cast<int32_t>(raw20) - RAW_ZERO_FIELD;
    }
}

namespace BSP::Mmc5603
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        return Begin(wire, address, Config{});
    }

    bool Begin(TwoWire &wire, uint8_t address, const Config &config)
    {
        s_wire = &wire;
        s_address = 0;
        s_initialized = false;
        s_ready = false;
        s_powered_down = true;
        s_last_error = Error::NotInitialized;
        s_diagnostics = {};
        s_diagnostics.requestedAddress = address;

        if (!ConfigValid(config))
            return Fail(Error::InvalidConfig, false);
        if (address != 0 && (address < FIRST_ADDRESS || address > LAST_ADDRESS))
            return Fail(Error::InvalidArgument, false);

        if (address != 0)
        {
            if (!ProbeAddress(address))
                return Fail(s_diagnostics.addressAcknowledged ? Error::IdentityMismatch : Error::DeviceNotFound, false);
            s_address = address;
        }
        else
        {
            for (uint8_t candidate = FIRST_ADDRESS; candidate <= LAST_ADDRESS; ++candidate)
            {
                if (ProbeAddress(candidate))
                {
                    s_address = candidate;
                    break;
                }
            }
            if (s_address == 0)
                return Fail(Error::DeviceNotFound, false);
        }

        s_initialized = true;
        s_config = config;
        return Reset();
    }

    bool Configure(const Config &config)
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (!ConfigValid(config))
            return Fail(Error::InvalidConfig, false);

        const uint8_t ctrl0 = static_cast<uint8_t>(CTRL0_CMM_FREQ_EN |
            (config.automaticSetReset ? CTRL0_AUTO_SR_EN : 0));
        const uint8_t ctrl1 = static_cast<uint8_t>(config.bandwidth) & 0x03;
        const uint8_t ctrl2 = static_cast<uint8_t>(CTRL2_CMM_EN |
            (config.periodicSet ? CTRL2_PERIODIC_SET_EN : 0) |
            (static_cast<uint8_t>(config.periodicSetInterval) & 0x07));

        s_diagnostics.configurationWritten = false;
        s_diagnostics.expectedOdr = config.outputRateHz;
        s_diagnostics.expectedCtrl0 = ctrl0;
        s_diagnostics.expectedCtrl1 = ctrl1;
        s_diagnostics.expectedCtrl2 = ctrl2;

        // 先退出连续模式，再依照数据手册的ODR→Cmm_freq_en→Cmm_en顺序启动。
        if (!WriteReg(REG_CTRL2, 0x00) ||
            !WriteReg(REG_CTRL1, ctrl1) ||
            !WriteReg(REG_ODR, config.outputRateHz) ||
            !WriteReg(REG_CTRL0, ctrl0) ||
            !WriteReg(REG_CTRL2, ctrl2))
        {
            return Fail(Error::BusError, true);
        }

        s_config = config;
        s_diagnostics.configurationWritten = true;
        s_powered_down = false;
        s_ready = true;
        ClearError();
        delay(10);
        return true;
    }

    bool GetConfig(Config *out)
    {
        if (!out || !s_initialized)
            return false;
        *out = s_config;
        return true;
    }

    bool Reset()
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (!WriteReg(REG_CTRL1, CTRL1_SW_RESET))
            return Fail(Error::BusError, true);
        s_diagnostics.configurationWritten = false;
        s_powered_down = true;
        delay(RESET_DELAY_MS);

        uint8_t product_id = 0;
        if (!ReadReg(REG_PRODUCT_ID, product_id))
            return Fail(Error::BusError, true);
        s_diagnostics.productIdValid = true;
        s_diagnostics.productId = product_id;
        if (product_id != PRODUCT_ID_VALUE)
            return Fail(Error::IdentityMismatch, true);
        if (!WaitForOtp())
            return false;
        return Configure(s_config);
    }

    bool PowerDown()
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (!WriteReg(REG_CTRL2, 0x00))
            return Fail(Error::BusError, true);
        s_powered_down = true;
        s_ready = true;
        ClearError();
        return true;
    }

    bool Wakeup()
    {
        return Configure(s_config);
    }

    bool IsPoweredDown() { return s_powered_down; }
    bool IsReady() { return s_initialized && s_ready && !s_powered_down; }
    bool ConfigurationWritten() { return s_ready && s_diagnostics.configurationWritten; }

    bool IsPresent(uint8_t address)
    {
        if (address != 0)
            return address >= FIRST_ADDRESS && address <= LAST_ADDRESS && ProbeAddress(address);
        for (uint8_t candidate = FIRST_ADDRESS; candidate <= LAST_ADDRESS; ++candidate)
            if (ProbeAddress(candidate))
                return true;
        return false;
    }

    uint8_t Address() { return s_address; }
    const char *TypeName() { return "MMC5603NJ"; }

    const char *ErrorName(Error error)
    {
        switch (error)
        {
        case Error::None: return "NONE";
        case Error::InvalidArgument: return "BAD_ARG";
        case Error::InvalidConfig: return "BAD_CFG";
        case Error::NotInitialized: return "NOT_INIT";
        case Error::DeviceNotFound: return "NO_DEVICE";
        case Error::IdentityMismatch: return "BAD_ID";
        case Error::OtpNotReady: return "OTP_WAIT";
        case Error::BusError: return "BUS";
        case Error::PoweredDown: return "POWER_DOWN";
        default: return "UNKNOWN";
        }
    }

    Error LastError() { return s_last_error; }

    bool GetDiagnostics(Diagnostics *out)
    {
        if (!out)
            return false;
        *out = s_diagnostics;
        return true;
    }

    TwoWire *Bus() { return s_wire; }

    bool ReadStatus(Status *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        *out = {};
        if (!s_initialized || !s_ready || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (s_powered_down)
            return Fail(Error::PoweredDown, false);

        uint8_t status = 0;
        if (!ReadReg(REG_STATUS1, status))
            return Fail(Error::BusError, true);
        s_diagnostics.statusValid = true;
        s_diagnostics.status = status;
        s_diagnostics.otpLoaded = (status & STATUS_OTP_DONE) != 0;
        out->temperatureReady = (status & STATUS_T_DONE) != 0;
        out->dataReady = (status & STATUS_M_DONE) != 0;
        out->selfTestSignal = (status & STATUS_SELF_TEST_SIGNAL) != 0;
        out->otpLoaded = s_diagnostics.otpLoaded;
        ClearError();
        return true;
    }

    bool Read(Reading *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        *out = {};
        if (!ReadStatus(&out->status))
            return false;
        if (!out->status.dataReady)
            return true;

        uint8_t data[9] = {};
        if (!ReadRegsAt(s_address, REG_DATA, data, sizeof(data)))
            return Fail(Error::BusError, true);
        out->xRaw = DecodeAxis(data[0], data[1], data[6]);
        out->yRaw = DecodeAxis(data[2], data[3], data[7]);
        out->zRaw = DecodeAxis(data[4], data[5], data[8]);
        out->xUt = static_cast<float>(out->xRaw) / COUNTS_PER_UT;
        out->yUt = static_cast<float>(out->yRaw) / COUNTS_PER_UT;
        out->zUt = static_cast<float>(out->zRaw) / COUNTS_PER_UT;
        ClearError();
        return true;
    }

    bool ReadRaw(Reading *out)
    {
        return Read(out) && out && out->status.dataReady;
    }

    bool PerformSet()
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (!WriteReg(REG_CTRL0, static_cast<uint8_t>(CTRL0_AUTO_SR_EN | CTRL0_DO_SET)))
            return Fail(Error::BusError, true);
        ClearError();
        delay(1);
        return true;
    }

    bool PerformReset()
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (!WriteReg(REG_CTRL0, static_cast<uint8_t>(CTRL0_AUTO_SR_EN | CTRL0_DO_RESET)))
            return Fail(Error::BusError, true);
        ClearError();
        delay(1);
        return true;
    }
}
