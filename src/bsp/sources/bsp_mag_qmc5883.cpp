/*
【模块职责】实现 QMC5883P/QMC5883L 共享 I2C 驱动。P 型寄存器和灵敏度依据 QST
QMC5883P Datasheet Rev E；L 型只保留项目旧驱动所需的兼容配置。
【错误策略】高频 Read() 不重试、不重建 Wire1；总线错误会清除 ready，由 SysMag 低频恢复。
*/
#include "bsp/bsp_mag_qmc5883.h"

namespace
{
    static constexpr uint8_t P_REG_CHIP_ID = 0x00;
    static constexpr uint8_t P_REG_DATA = 0x01;
    static constexpr uint8_t P_REG_STATUS = 0x09;
    static constexpr uint8_t P_REG_CTRL1 = 0x0A;
    static constexpr uint8_t P_REG_CTRL2 = 0x0B;
    static constexpr uint8_t P_REG_AXIS_SIGN = 0x29;
    static constexpr uint8_t P_AXIS_SIGN_VALUE = 0x06;
    static constexpr uint8_t P_CTRL2_SOFT_RESET = 0x80;
    static constexpr uint8_t P_STATUS_DRDY = 0x01;
    static constexpr uint8_t P_STATUS_OVERFLOW = 0x02;

    static constexpr uint8_t L_REG_DATA = 0x00;
    static constexpr uint8_t L_REG_STATUS = 0x06;
    static constexpr uint8_t L_REG_CTRL1 = 0x09;
    static constexpr uint8_t L_REG_CTRL2 = 0x0A;
    static constexpr uint8_t L_REG_SET_RESET = 0x0B;
    static constexpr uint8_t L_REG_CHIP_ID = 0x0D;
    static constexpr uint8_t L_STATUS_DRDY = 0x01;
    static constexpr uint8_t L_STATUS_OVERFLOW = 0x02;
    static constexpr uint8_t L_CTRL1_200HZ_8G_OSR512 = 0x1D;

    TwoWire *s_wire = &Wire1;
    uint8_t s_address = 0;
    BSP::Qmc5883::Type s_type = BSP::Qmc5883::Type::None;
    BSP::Qmc5883::Config s_config = {};
    BSP::Qmc5883::Error s_last_error = BSP::Qmc5883::Error::NotInitialized;
    BSP::Qmc5883::Diagnostics s_diagnostics = {};
    bool s_initialized = false;
    bool s_ready = false;
    bool s_powered_down = true;
    float s_ut_per_lsb = 0.0f;

    bool Fail(BSP::Qmc5883::Error error, bool mark_not_ready)
    {
        s_last_error = error;
        if (mark_not_ready)
            s_ready = false;
        return false;
    }

    void ClearError()
    {
        s_last_error = BSP::Qmc5883::Error::None;
    }

    int16_t I16Le(uint8_t lo, uint8_t hi)
    {
        return static_cast<int16_t>(static_cast<uint16_t>(lo) |
                                    (static_cast<uint16_t>(hi) << 8));
    }

    bool AckAt(uint8_t address)
    {
        if (!s_wire || address == 0)
            return false;
        s_wire->beginTransmission(address);
        return s_wire->endTransmission() == 0;
    }

    bool ReadRegsAt(uint8_t address, uint8_t reg, uint8_t *data, size_t len)
    {
        if (!s_wire || address == 0 || !data || len == 0)
            return false;

        s_wire->beginTransmission(address);
        s_wire->write(reg);
        if (s_wire->endTransmission(false) != 0)
            return false;

        const size_t count = s_wire->requestFrom(static_cast<uint16_t>(address), len, true);
        if (count != len)
        {
            while (s_wire->available())
                s_wire->read();
            return false;
        }
        for (size_t index = 0; index < len; ++index)
            data[index] = static_cast<uint8_t>(s_wire->read());
        return true;
    }

    bool ReadRegs(uint8_t reg, uint8_t *data, size_t len)
    {
        return ReadRegsAt(s_address, reg, data, len);
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

    bool ReadReg(uint8_t reg, uint8_t &value)
    {
        return ReadRegs(reg, &value, 1);
    }

    bool ProbeP(uint8_t address)
    {
        s_diagnostics.requestedAddress = address;
        s_diagnostics.addressAcknowledged = AckAt(address);
        if (!s_diagnostics.addressAcknowledged)
            return false;
        uint8_t identity = 0;
        s_diagnostics.chipIdValid = ReadRegsAt(address, P_REG_CHIP_ID, &identity, 1);
        s_diagnostics.chipId = identity;
        return s_diagnostics.chipIdValid && identity == BSP::Qmc5883::CHIP_ID_QMC5883P;
    }

    bool ProbeL(uint8_t address)
    {
        uint8_t identity = 0;
        return AckAt(address) && ReadRegsAt(address, L_REG_CHIP_ID, &identity, 1);
    }

    bool EncodePConfig(const BSP::Qmc5883::Config &config,
                       uint8_t &ctrl1,
                       uint8_t &ctrl2,
                       float &ut_per_lsb)
    {
        using namespace BSP::Qmc5883;
        uint8_t range_bits = 0;
        float lsb_per_gauss = 0.0f;
        switch (config.range)
        {
        case Range::G30: range_bits = 0x00; lsb_per_gauss = 1000.0f; break;
        case Range::G12: range_bits = 0x04; lsb_per_gauss = 2500.0f; break;
        case Range::G8: range_bits = 0x08; lsb_per_gauss = 3750.0f; break;
        case Range::G2: range_bits = 0x0C; lsb_per_gauss = 15000.0f; break;
        default: return false;
        }

        uint8_t odr_bits = 0;
        switch (config.outputRate)
        {
        case OutputDataRate::Hz10: odr_bits = 0x00; break;
        case OutputDataRate::Hz50: odr_bits = 0x04; break;
        case OutputDataRate::Hz100: odr_bits = 0x08; break;
        case OutputDataRate::Hz200: odr_bits = 0x0C; break;
        default: return false;
        }

        uint8_t osr1_bits = 0;
        uint8_t osr2_bits = 0;
        switch (config.oversampling1)
        {
        case Oversampling::X8: osr1_bits = 0x00; break;
        case Oversampling::X4: osr1_bits = 0x10; break;
        case Oversampling::X2: osr1_bits = 0x20; break;
        case Oversampling::X1: osr1_bits = 0x30; break;
        default: return false;
        }
        switch (config.oversampling2)
        {
        case Oversampling::X8: osr2_bits = 0xC0; break;
        case Oversampling::X4: osr2_bits = 0x80; break;
        case Oversampling::X2: osr2_bits = 0x40; break;
        case Oversampling::X1: osr2_bits = 0x00; break;
        default: return false;
        }

        // MODE=01 为 Normal；SET/RESET=00 表示每次测量均执行 Set and Reset。
        ctrl1 = static_cast<uint8_t>(osr2_bits | osr1_bits | odr_bits | 0x01);
        ctrl2 = range_bits;
        ut_per_lsb = 100.0f / lsb_per_gauss; // 1 Gauss = 100 uT。
        return true;
    }

    bool ConfigureP(const BSP::Qmc5883::Config &config, bool save_config)
    {
        uint8_t ctrl1 = 0;
        uint8_t ctrl2 = 0;
        float ut_per_lsb = 0.0f;
        if (!EncodePConfig(config, ctrl1, ctrl2, ut_per_lsb))
            return Fail(BSP::Qmc5883::Error::InvalidConfig, false);
        s_diagnostics.expectedCtrl1 = ctrl1;
        s_diagnostics.expectedCtrl2 = ctrl2;

        /*
         * Rev E 要求 Normal/Single/Continuous 互相切换时经过 Suspend。0x29=0x06 是厂商所有
         * P 型应用例程的轴符号初始化；它只定义芯片输出符号，不等价于 V4B 的 SensorToBody。
         */
        if (!WriteReg(P_REG_CTRL1, 0x00) ||
            !WriteReg(P_REG_AXIS_SIGN, P_AXIS_SIGN_VALUE) ||
            !WriteReg(P_REG_CTRL2, ctrl2) ||
            !WriteReg(P_REG_CTRL1, ctrl1))
        {
            return Fail(BSP::Qmc5883::Error::BusError, true);
        }

        uint8_t verify_ctrl1 = 0;
        uint8_t verify_ctrl2 = 0;
        if (!ReadReg(P_REG_CTRL1, verify_ctrl1) ||
            !ReadReg(P_REG_CTRL2, verify_ctrl2))
        {
            return Fail(BSP::Qmc5883::Error::BusError, true);
        }
        s_diagnostics.ctrl1Valid = true;
        s_diagnostics.ctrl1 = verify_ctrl1;
        s_diagnostics.ctrl1Matches = verify_ctrl1 == ctrl1;
        s_diagnostics.ctrl2Valid = true;
        s_diagnostics.ctrl2 = verify_ctrl2;
        s_diagnostics.ctrl2Matches = verify_ctrl2 == ctrl2;
        if (!s_diagnostics.ctrl1Matches || !s_diagnostics.ctrl2Matches)
            return Fail(BSP::Qmc5883::Error::RegisterVerifyFailed, true);

        /*
         * 0x29是Rev E应用例程要求写0x06的保留寄存器，但不在公开寄存器表中，也没有承诺
         * 可回读。继续记录它的实板读值供诊断，不把读失败或值不同当成启动失败；真正决定
         * 工作模式和量程的CTRL1/CTRL2仍在上面严格回读验证。
         */
        uint8_t verify_axis = 0;
        s_diagnostics.axisSignValid = ReadReg(P_REG_AXIS_SIGN, verify_axis);
        s_diagnostics.axisSign = verify_axis;

        if (save_config)
            s_config = config;
        s_ut_per_lsb = ut_per_lsb;
        s_powered_down = false;
        s_ready = true;
        ClearError();
        delay(20);
        return true;
    }

    bool ConfigureL()
    {
        if (!WriteReg(L_REG_SET_RESET, 0x01) ||
            !WriteReg(L_REG_CTRL2, 0x00) ||
            !WriteReg(L_REG_CTRL1, L_CTRL1_200HZ_8G_OSR512))
        {
            return Fail(BSP::Qmc5883::Error::BusError, true);
        }
        s_ut_per_lsb = 100.0f / 3000.0f;
        s_powered_down = false;
        s_ready = true;
        ClearError();
        delay(20);
        return true;
    }
}

namespace BSP::Qmc5883
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        return Begin(wire, address, Config{});
    }

    bool Begin(TwoWire &wire, uint8_t address, const Config &config)
    {
        s_wire = &wire;
        s_address = 0;
        s_type = Type::None;
        s_initialized = false;
        s_ready = false;
        s_powered_down = true;
        s_ut_per_lsb = 0.0f;
        s_last_error = Error::NotInitialized;
        s_diagnostics = {};
        s_diagnostics.requestedAddress = address;

        uint8_t unused_ctrl1 = 0;
        uint8_t unused_ctrl2 = 0;
        float unused_scale = 0.0f;
        if (!EncodePConfig(config, unused_ctrl1, unused_ctrl2, unused_scale))
            return Fail(Error::InvalidConfig, false);

        if (address == ADDRESS_QMC5883P || (address == 0 && ProbeP(ADDRESS_QMC5883P)))
        {
            if (!ProbeP(ADDRESS_QMC5883P))
                return Fail(AckAt(ADDRESS_QMC5883P) ? Error::IdentityMismatch : Error::DeviceNotFound, false);
            s_address = ADDRESS_QMC5883P;
            s_type = Type::Qmc5883p;
        }
        else if (address == ADDRESS_QMC5883L || (address == 0 && ProbeL(ADDRESS_QMC5883L)))
        {
            if (!ProbeL(ADDRESS_QMC5883L))
                return Fail(AckAt(ADDRESS_QMC5883L) ? Error::IdentityMismatch : Error::DeviceNotFound, false);
            s_address = ADDRESS_QMC5883L;
            s_type = Type::Qmc5883l;
        }
        else
        {
            return Fail(Error::DeviceNotFound, false);
        }

        s_initialized = true;
        if (s_type == Type::Qmc5883p)
        {
            if (!WriteReg(P_REG_CTRL2, P_CTRL2_SOFT_RESET))
                return Fail(Error::BusError, true);
            delay(2);
            uint8_t identity = 0;
            if (!ReadReg(P_REG_CHIP_ID, identity))
                return Fail(Error::BusError, true);
            if (identity != CHIP_ID_QMC5883P)
                return Fail(Error::IdentityMismatch, true);
            return ConfigureP(config, true);
        }
        return ConfigureL();
    }

    bool Configure(const Config &config)
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (s_type == Type::Qmc5883p)
            return ConfigureP(config, true);

        // L 型兼容分支的寄存器含义不同；仅验证配置合法后恢复固定旧配置。
        uint8_t unused_ctrl1 = 0;
        uint8_t unused_ctrl2 = 0;
        float unused_scale = 0.0f;
        if (!EncodePConfig(config, unused_ctrl1, unused_ctrl2, unused_scale))
            return Fail(Error::InvalidConfig, false);
        s_config = config;
        return ConfigureL();
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
        if (s_type == Type::Qmc5883p)
        {
            if (!WriteReg(P_REG_CTRL2, P_CTRL2_SOFT_RESET))
                return Fail(Error::BusError, true);
            delay(2);
            uint8_t identity = 0;
            if (!ReadReg(P_REG_CHIP_ID, identity))
                return Fail(Error::BusError, true);
            if (identity != CHIP_ID_QMC5883P)
                return Fail(Error::IdentityMismatch, true);
            return ConfigureP(s_config, false);
        }

        if (!WriteReg(L_REG_CTRL2, 0x80))
            return Fail(Error::BusError, true);
        delay(2);
        return ConfigureL();
    }

    bool PowerDown()
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        const uint8_t ctrl = s_type == Type::Qmc5883p ? P_REG_CTRL1 : L_REG_CTRL1;
        if (!WriteReg(ctrl, 0x00))
            return Fail(Error::BusError, true);
        s_powered_down = true;
        s_ready = true;
        ClearError();
        return true;
    }

    bool Wakeup()
    {
        if (!s_initialized || s_address == 0)
            return Fail(Error::NotInitialized, false);
        return s_type == Type::Qmc5883p ? ConfigureP(s_config, false) : ConfigureL();
    }

    bool IsPoweredDown()
    {
        return s_powered_down;
    }

    bool IsReady()
    {
        return s_initialized && s_ready && !s_powered_down;
    }

    bool RangeConfigurationVerified()
    {
        return s_type == Type::Qmc5883l ||
               (s_type == Type::Qmc5883p && s_ready &&
                s_diagnostics.ctrl1Matches && s_diagnostics.ctrl2Matches);
    }

    bool IsPresent(uint8_t address)
    {
        if (address == ADDRESS_QMC5883P)
            return ProbeP(address);
        if (address == ADDRESS_QMC5883L)
            return ProbeL(address);
        if (address != 0)
            return false;
        return ProbeP(ADDRESS_QMC5883P) || ProbeL(ADDRESS_QMC5883L);
    }

    uint8_t Address() { return s_address; }
    Type SensorType() { return s_type; }

    const char *TypeName(Type type)
    {
        switch (type)
        {
        case Type::Qmc5883p: return "QMC5883P";
        case Type::Qmc5883l: return "QMC5883L";
        default: return "QMC5883";
        }
    }

    const char *TypeName() { return TypeName(s_type); }

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
        case Error::BusError: return "BUS";
        case Error::RegisterVerifyFailed: return "REG_VERIFY";
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

    bool Read(Reading *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        *out = {};
        if (!s_initialized || !s_ready || s_address == 0)
            return Fail(Error::NotInitialized, false);
        if (s_powered_down)
            return Fail(Error::PoweredDown, false);

        const uint8_t status_reg = s_type == Type::Qmc5883p ? P_REG_STATUS : L_REG_STATUS;
        const uint8_t data_reg = s_type == Type::Qmc5883p ? P_REG_DATA : L_REG_DATA;
        const uint8_t drdy_mask = s_type == Type::Qmc5883p ? P_STATUS_DRDY : L_STATUS_DRDY;
        const uint8_t overflow_mask = s_type == Type::Qmc5883p ? P_STATUS_OVERFLOW : L_STATUS_OVERFLOW;

        uint8_t status = 0;
        if (!ReadReg(status_reg, status))
            return Fail(Error::BusError, true);
        s_diagnostics.statusValid = true;
        s_diagnostics.status = status;
        out->status.dataReady = (status & drdy_mask) != 0;
        out->status.overflow = (status & overflow_mask) != 0;
        if (!out->status.dataReady)
        {
            ClearError();
            return true;
        }

        uint8_t data[6] = {};
        if (!ReadRegs(data_reg, data, sizeof(data)))
            return Fail(Error::BusError, true);
        out->xRaw = I16Le(data[0], data[1]);
        out->yRaw = I16Le(data[2], data[3]);
        out->zRaw = I16Le(data[4], data[5]);
        out->xUt = static_cast<float>(out->xRaw) * s_ut_per_lsb;
        out->yUt = static_cast<float>(out->yRaw) * s_ut_per_lsb;
        out->zUt = static_cast<float>(out->zRaw) * s_ut_per_lsb;
        ClearError();
        return true;
    }

    bool ReadRaw(Reading *out)
    {
        if (!Read(out))
            return false;
        return out->status.dataReady && !out->status.overflow;
    }
}
