/*
【模块职责】实现 LSM6DSV 的 I2C 寄存器访问、输出换算、配置回读和 FIFO 基础操作。
【实现策略】初始化时开启 BDU 与地址自动递增；普通采样先读取 STATUS_REG，无新数据时立即返回，
有新数据时从 OUT_TEMP_L 连续读取 14 字节。FIFO 使用芯片原生 7 字节 word（tag + 三轴 16 位数据），
不在 BSP 内猜测不同 tag 的物理单位。
【错误策略】高频 Read() 不自动重试、不刷串口；总线错误会清除 ready，由 SYS 层按统一节拍调用
Reset()/Begin() 恢复。FIFO 状态读取遵循手册语义，读取 STATUS2 会清除 overrun latched 位。
*/
#include "bsp/bsp_imu_lsm6dsv.h"
#include "bsp/bsp_pins.h"

namespace
{
    static constexpr uint8_t REG_WHO_AM_I = 0x0F;
    static constexpr uint8_t REG_CTRL1 = 0x10;
    static constexpr uint8_t REG_CTRL2 = 0x11;
    static constexpr uint8_t REG_CTRL3 = 0x12;
    static constexpr uint8_t REG_CTRL6 = 0x15;
    static constexpr uint8_t REG_CTRL8 = 0x17;
    static constexpr uint8_t REG_STATUS = 0x1E;
    static constexpr uint8_t REG_OUT_TEMP_L = 0x20;
    static constexpr uint8_t REG_FIFO_CTRL1 = 0x07;
    static constexpr uint8_t REG_FIFO_STATUS1 = 0x1B;
    static constexpr uint8_t REG_FIFO_DATA_TAG = 0x78;

    static constexpr uint8_t CTRL3_BDU = 0x40;
    static constexpr uint8_t CTRL3_IF_INC = 0x04;
    static constexpr uint8_t CTRL3_SW_RESET = 0x01;
    static constexpr uint8_t STATUS_TDA = 0x04;
    static constexpr uint8_t STATUS_GDA = 0x02;
    static constexpr uint8_t STATUS_XLDA = 0x01;

    static constexpr uint8_t FIFO_STATUS_WTM = 0x80;
    static constexpr uint8_t FIFO_STATUS_OVR = 0x40;
    static constexpr uint8_t FIFO_STATUS_FULL = 0x20;
    static constexpr uint8_t FIFO_STATUS_OVR_LATCHED = 0x08;
    static constexpr uint8_t FIFO_STATUS_DIFF_HIGH = 0x01;

    static constexpr uint8_t FIFO_MODE_BYPASS = 0x00;
    static constexpr uint8_t FIFO_MODE_STOP_ON_FULL = 0x01;
    static constexpr uint8_t FIFO_MODE_CONTINUOUS = 0x06;
    static constexpr uint32_t RESET_TIMEOUT_MS = 100;
    static constexpr uint32_t SENSOR_TURN_ON_MS = 30;

    TwoWire *s_wire = &Wire1;
    uint8_t s_address = 0;
    bool s_initialized = false;
    bool s_ready = false;
    bool s_powered_down = true;
    BSP::Lsm6dsv::Config s_config = {};
    BSP::Lsm6dsv::FifoConfig s_fifo_config = {};
    BSP::Lsm6dsv::Error s_last_error = BSP::Lsm6dsv::Error::NotInitialized;

    int16_t I16Le(uint8_t lo, uint8_t hi)
    {
        return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    }

    bool Fail(BSP::Lsm6dsv::Error error, bool mark_not_ready)
    {
        s_last_error = error;
        if (mark_not_ready)
            s_ready = false;
        return false;
    }

    void ClearError()
    {
        s_last_error = BSP::Lsm6dsv::Error::None;
    }

    bool Ack(uint8_t address)
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

        const size_t count = s_wire->requestFrom((uint16_t)address, len, true);
        if (count != len)
        {
            while (s_wire->available())
                s_wire->read();
            return false;
        }

        for (size_t index = 0; index < len; ++index)
            data[index] = (uint8_t)s_wire->read();
        return true;
    }

    bool ReadRegs(uint8_t reg, uint8_t *data, size_t len)
    {
        return ReadRegsAt(s_address, reg, data, len);
    }

    bool WriteRegs(uint8_t reg, const uint8_t *data, size_t len)
    {
        if (!s_wire || s_address == 0 || !data || len == 0)
            return false;
        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        for (size_t index = 0; index < len; ++index)
            s_wire->write(data[index]);
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

    bool ReadIdentity(uint8_t address, uint8_t &identity)
    {
        return ReadRegsAt(address, REG_WHO_AM_I, &identity, 1);
    }

    bool ProbeAddress(uint8_t address)
    {
        uint8_t identity = 0;
        return Ack(address) && ReadIdentity(address, identity) && identity == BSP::Lsm6dsv::WHO_AM_I_VALUE;
    }

    bool EncodeOdr(BSP::Lsm6dsv::OutputDataRate rate, uint8_t &bits, bool gyro)
    {
        using BSP::Lsm6dsv::OutputDataRate;
        switch (rate)
        {
        case OutputDataRate::PowerDown: bits = 0x00; return true;
        case OutputDataRate::Hz1_875:
            if (gyro)
                return false;
            bits = 0x01;
            return true;
        case OutputDataRate::Hz7_5: bits = 0x02; return true;
        case OutputDataRate::Hz15: bits = 0x03; return true;
        case OutputDataRate::Hz30: bits = 0x04; return true;
        case OutputDataRate::Hz60: bits = 0x05; return true;
        case OutputDataRate::Hz120: bits = 0x06; return true;
        case OutputDataRate::Hz240: bits = 0x07; return true;
        case OutputDataRate::Hz480: bits = 0x08; return true;
        case OutputDataRate::Hz960: bits = 0x09; return true;
        case OutputDataRate::Hz1920: bits = 0x0A; return true;
        case OutputDataRate::Hz3840: bits = 0x0B; return true;
        case OutputDataRate::Hz7680: bits = 0x0C; return true;
        }
        return false;
    }

    bool EncodeBatchRate(BSP::Lsm6dsv::OutputDataRate rate, uint8_t &bits)
    {
        // FIFO BDR 编码与主输出 ODR 使用相同的低四位编码。
        return EncodeOdr(rate, bits, false);
    }

    bool EncodeTemperatureBatchRate(BSP::Lsm6dsv::OutputDataRate rate, uint8_t &bits)
    {
        using BSP::Lsm6dsv::OutputDataRate;
        switch (rate)
        {
        case OutputDataRate::PowerDown: bits = 0x00; return true;
        case OutputDataRate::Hz1_875: bits = 0x01; return true;
        case OutputDataRate::Hz15: bits = 0x02; return true;
        case OutputDataRate::Hz60: bits = 0x03; return true;
        default: return false;
        }
    }

    bool EncodeAccelRange(BSP::Lsm6dsv::AccelRange range, uint8_t &bits, float &g_per_lsb)
    {
        using BSP::Lsm6dsv::AccelRange;
        switch (range)
        {
        case AccelRange::G2: bits = 0x00; g_per_lsb = 0.000061f; return true;
        case AccelRange::G4: bits = 0x01; g_per_lsb = 0.000122f; return true;
        case AccelRange::G8: bits = 0x02; g_per_lsb = 0.000244f; return true;
        case AccelRange::G16: bits = 0x03; g_per_lsb = 0.000488f; return true;
        }
        return false;
    }

    bool EncodeGyroRange(BSP::Lsm6dsv::GyroRange range, uint8_t &bits, float &dps_per_lsb)
    {
        using BSP::Lsm6dsv::GyroRange;
        switch (range)
        {
        case GyroRange::Dps125: bits = 0x00; dps_per_lsb = 0.004375f; return true;
        case GyroRange::Dps250: bits = 0x01; dps_per_lsb = 0.00875f; return true;
        case GyroRange::Dps500: bits = 0x02; dps_per_lsb = 0.01750f; return true;
        case GyroRange::Dps1000: bits = 0x03; dps_per_lsb = 0.035f; return true;
        case GyroRange::Dps2000: bits = 0x04; dps_per_lsb = 0.070f; return true;
        case GyroRange::Dps4000: bits = 0x0C; dps_per_lsb = 0.140f; return true;
        }
        return false;
    }

    bool EncodeConfig(const BSP::Lsm6dsv::Config &config,
                      uint8_t &ctrl1,
                      uint8_t &ctrl2,
                      uint8_t &ctrl6,
                      uint8_t &ctrl8,
                      float &accel_scale,
                      float &gyro_scale)
    {
        uint8_t accel_odr = 0;
        uint8_t gyro_odr = 0;
        uint8_t accel_range = 0;
        uint8_t gyro_range = 0;
        if (!EncodeOdr(config.accelRate, accel_odr, false) ||
            !EncodeOdr(config.gyroRate, gyro_odr, true) ||
            !EncodeAccelRange(config.accelRange, accel_range, accel_scale) ||
            !EncodeGyroRange(config.gyroRange, gyro_range, gyro_scale))
        {
            return false;
        }

        // LSM6DSV 的 ODR 在高四位不再布局，CTRL1/2 只写低四位 ODR。
        ctrl1 = accel_odr;
        ctrl2 = gyro_odr;
        ctrl6 = gyro_range;
        ctrl8 = accel_range;
        return true;
    }

    bool WriteAndVerifyConfig(const BSP::Lsm6dsv::Config &config, bool save_config)
    {
        uint8_t ctrl1 = 0;
        uint8_t ctrl2 = 0;
        uint8_t ctrl6 = 0;
        uint8_t ctrl8 = 0;
        float unused_accel_scale = 0.0f;
        float unused_gyro_scale = 0.0f;
        if (!EncodeConfig(config, ctrl1, ctrl2, ctrl6, ctrl8, unused_accel_scale, unused_gyro_scale))
            return Fail(BSP::Lsm6dsv::Error::InvalidConfig, false);

        // BDU 和 IF_INC 是后续所有连续读的前提，先写 CTRL3，再写各独立量程寄存器。
        if (!WriteReg(REG_CTRL3, CTRL3_BDU | CTRL3_IF_INC) ||
            !WriteReg(REG_CTRL1, ctrl1) ||
            !WriteReg(REG_CTRL2, ctrl2) ||
            !WriteReg(REG_CTRL6, ctrl6) ||
            !WriteReg(REG_CTRL8, ctrl8))
        {
            return Fail(BSP::Lsm6dsv::Error::BusError, true);
        }

        uint8_t verify[9] = {};
        if (!ReadRegs(REG_CTRL1, verify, sizeof(verify)))
            return Fail(BSP::Lsm6dsv::Error::BusError, true);
        if (verify[0] != ctrl1 || verify[1] != ctrl2 ||
            (verify[2] & (CTRL3_BDU | CTRL3_IF_INC)) != (CTRL3_BDU | CTRL3_IF_INC) ||
            verify[5] != ctrl6 || verify[7] != ctrl8)
        {
            return Fail(BSP::Lsm6dsv::Error::RegisterVerifyFailed, true);
        }

        if (save_config)
            s_config = config;
        s_powered_down = config.accelRate == BSP::Lsm6dsv::OutputDataRate::PowerDown &&
                         config.gyroRate == BSP::Lsm6dsv::OutputDataRate::PowerDown;
        if (!s_powered_down)
            delay(SENSOR_TURN_ON_MS);
        s_ready = true;
        ClearError();
        return true;
    }

    bool ValidateFifoConfig(const BSP::Lsm6dsv::FifoConfig &config,
                            uint8_t &ctrl1,
                            uint8_t &ctrl2,
                            uint8_t &ctrl3,
                            uint8_t &ctrl4)
    {
        if (config.enabled && config.watermarkWords == 0)
            return false;

        uint8_t accel_bdr = 0;
        uint8_t gyro_bdr = 0;
        uint8_t temp_bdr = 0;
        if (!EncodeBatchRate(config.enabled ? config.accelBatchRate : BSP::Lsm6dsv::OutputDataRate::PowerDown, accel_bdr) ||
            !EncodeBatchRate(config.enabled ? config.gyroBatchRate : BSP::Lsm6dsv::OutputDataRate::PowerDown, gyro_bdr) ||
            !EncodeTemperatureBatchRate(config.enabled ? config.temperatureBatchRate : BSP::Lsm6dsv::OutputDataRate::PowerDown, temp_bdr))
        {
            return false;
        }

        ctrl1 = config.enabled ? config.watermarkWords : 0;
        ctrl2 = 0;
        ctrl3 = (uint8_t)((gyro_bdr << 4) | accel_bdr);
        ctrl4 = (uint8_t)((temp_bdr << 4) |
                          (config.enabled ? (config.continuous ? FIFO_MODE_CONTINUOUS : FIFO_MODE_STOP_ON_FULL)
                                           : FIFO_MODE_BYPASS));
        return true;
    }

    bool ApplyFifoConfig(const BSP::Lsm6dsv::FifoConfig &config, bool save_config)
    {
        uint8_t ctrl1 = 0;
        uint8_t ctrl2 = 0;
        uint8_t ctrl3 = 0;
        uint8_t ctrl4 = 0;
        if (!ValidateFifoConfig(config, ctrl1, ctrl2, ctrl3, ctrl4))
            return Fail(BSP::Lsm6dsv::Error::InvalidConfig, false);

        const uint8_t values[4] = {ctrl1, ctrl2, ctrl3, ctrl4};
        if (!WriteRegs(REG_FIFO_CTRL1, values, sizeof(values)))
            return Fail(BSP::Lsm6dsv::Error::BusError, true);

        uint8_t verify[4] = {};
        if (!ReadRegs(REG_FIFO_CTRL1, verify, sizeof(verify)))
            return Fail(BSP::Lsm6dsv::Error::BusError, true);
        if (verify[0] != ctrl1 || verify[1] != ctrl2 || verify[2] != ctrl3 || verify[3] != ctrl4)
            return Fail(BSP::Lsm6dsv::Error::RegisterVerifyFailed, true);

        if (save_config)
            s_fifo_config = config;
        ClearError();
        return true;
    }

    bool SoftwareReset()
    {
        if (!WriteReg(REG_CTRL3, CTRL3_SW_RESET))
            return Fail(BSP::Lsm6dsv::Error::BusError, true);

        const uint32_t started_ms = millis();
        while (millis() - started_ms < RESET_TIMEOUT_MS)
        {
            uint8_t ctrl3 = 0;
            if (!ReadReg(REG_CTRL3, ctrl3))
                return Fail(BSP::Lsm6dsv::Error::BusError, true);
            if ((ctrl3 & CTRL3_SW_RESET) == 0)
            {
                uint8_t identity = 0;
                if (!ReadReg(REG_WHO_AM_I, identity))
                    return Fail(BSP::Lsm6dsv::Error::BusError, true);
                if (identity != BSP::Lsm6dsv::WHO_AM_I_VALUE)
                    return Fail(BSP::Lsm6dsv::Error::IdentityMismatch, true);
                return true;
            }
            delay(1);
        }
        return Fail(BSP::Lsm6dsv::Error::ResetTimeout, true);
    }

    BSP::Lsm6dsv::FifoTag DecodeFifoTag(uint8_t tag)
    {
        using BSP::Lsm6dsv::FifoTag;
        switch (tag)
        {
        case 0x00: return FifoTag::Empty;
        case 0x01: return FifoTag::Gyroscope;
        case 0x02: return FifoTag::Accelerometer;
        case 0x03: return FifoTag::Temperature;
        case 0x04: return FifoTag::Timestamp;
        case 0x05: return FifoTag::ConfigChange;
        case 0x13: return FifoTag::GameRotationVector;
        case 0x16: return FifoTag::GyroscopeBias;
        case 0x17: return FifoTag::GravityVector;
        default: return FifoTag::Unknown;
        }
    }
}

namespace BSP::Lsm6dsv
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        const Config default_config = {};
        return Begin(wire, address, default_config);
    }

    bool Begin(TwoWire &wire, uint8_t address, const Config &config)
    {
        s_wire = &wire;
        s_address = 0;
        s_initialized = false;
        s_ready = false;
        s_powered_down = true;
        s_fifo_config = {};
        s_last_error = Error::NotInitialized;

        uint8_t unused_ctrl1 = 0;
        uint8_t unused_ctrl2 = 0;
        uint8_t unused_ctrl6 = 0;
        uint8_t unused_ctrl8 = 0;
        float unused_accel_scale = 0.0f;
        float unused_gyro_scale = 0.0f;
        if (!EncodeConfig(config, unused_ctrl1, unused_ctrl2, unused_ctrl6, unused_ctrl8,
                          unused_accel_scale, unused_gyro_scale))
            return Fail(Error::InvalidConfig, false);

        if (s_wire == &Wire1)
        {
            // IMU 与 RTC/TM6605 共用总线，沿用 100 kHz 和 20 ms 超时，避免改变其他 BSP 的时序假设。
            s_wire->begin(Pins::I2C_SDA, Pins::I2C_SCL);
            s_wire->setClock(100000);
            s_wire->setTimeOut(20);
        }

        bool any_ack = false;
        if (address != 0)
        {
            any_ack = Ack(address);
            uint8_t identity = 0;
            if (!any_ack)
                return Fail(Error::DeviceNotFound, false);
            if (!ReadIdentity(address, identity))
                return Fail(Error::BusError, false);
            if (identity != WHO_AM_I_VALUE)
                return Fail(Error::IdentityMismatch, false);
            s_address = address;
        }
        else
        {
            any_ack = Ack(ADDRESS_LOW);
            if (ProbeAddress(ADDRESS_LOW))
                s_address = ADDRESS_LOW;
            else
            {
                any_ack = Ack(ADDRESS_HIGH) || any_ack;
                if (ProbeAddress(ADDRESS_HIGH))
                    s_address = ADDRESS_HIGH;
            }
            if (s_address == 0)
                return Fail(any_ack ? Error::IdentityMismatch : Error::DeviceNotFound, false);
        }

        s_config = config;
        s_initialized = true;
        if (!SoftwareReset() || !WriteAndVerifyConfig(config, true) || !ApplyFifoConfig(s_fifo_config, true))
        {
            Serial.printf("[BSP][IMU] LSM6DSV 初始化失败：地址=0x%02X，错误码=%u。\n",
                          s_address, (unsigned)s_last_error);
            return false;
        }
        return true;
    }

    bool Configure(const Config &config)
    {
        if (!s_initialized)
            return Fail(Error::NotInitialized, false);
        return WriteAndVerifyConfig(config, true);
    }

    bool GetConfig(Config *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        if (!s_initialized)
            return Fail(Error::NotInitialized, false);
        *out = s_config;
        ClearError();
        return true;
    }

    bool Reset()
    {
        if (!s_initialized || !s_wire || s_address == 0)
            return Fail(Error::NotInitialized, false);
        s_ready = false;
        s_powered_down = true;
        return SoftwareReset() && WriteAndVerifyConfig(s_config, false) && ApplyFifoConfig(s_fifo_config, false);
    }

    bool PowerDown()
    {
        if (!s_initialized)
            return Fail(Error::NotInitialized, false);
        if (!s_ready)
            return Fail(Error::BusError, true);
        if (s_powered_down)
        {
            ClearError();
            return true;
        }

        Config power_down = s_config;
        power_down.accelRate = OutputDataRate::PowerDown;
        power_down.gyroRate = OutputDataRate::PowerDown;
        if (!WriteAndVerifyConfig(power_down, false))
            return false;

        // FIFO 批处理配置保留在 s_fifo_config，休眠期间只旁路硬件，唤醒后再恢复。
        const FifoConfig bypass = {};
        return ApplyFifoConfig(bypass, false);
    }

    bool Wakeup()
    {
        if (!s_initialized)
            return Fail(Error::NotInitialized, false);
        if (!s_powered_down && s_ready)
        {
            ClearError();
            return true;
        }
        return WriteAndVerifyConfig(s_config, false) && ApplyFifoConfig(s_fifo_config, false);
    }

    bool IsReady()
    {
        return s_initialized && s_ready;
    }

    bool IsPoweredDown()
    {
        return s_initialized && s_powered_down;
    }

    bool IsPresent(uint8_t address)
    {
        if (address != 0)
            return ProbeAddress(address);
        return ProbeAddress(ADDRESS_LOW) || ProbeAddress(ADDRESS_HIGH);
    }

    uint8_t Address()
    {
        return s_address;
    }

    TwoWire *Bus()
    {
        return s_wire;
    }

    Error LastError()
    {
        return s_last_error;
    }

    bool ReadStatus(DataReady *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        if (!s_initialized || !s_ready)
            return Fail(Error::NotInitialized, false);
        if (s_powered_down)
            return Fail(Error::PoweredDown, false);

        uint8_t status = 0;
        if (!ReadReg(REG_STATUS, status))
            return Fail(Error::BusError, true);
        out->accel = (status & STATUS_XLDA) != 0;
        out->gyro = (status & STATUS_GDA) != 0;
        out->temperature = (status & STATUS_TDA) != 0;
        ClearError();
        return true;
    }

    bool Read(Reading *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        if (!s_initialized || !s_ready)
            return Fail(Error::NotInitialized, false);
        if (s_powered_down)
            return Fail(Error::PoweredDown, false);

        uint8_t status = 0;
        if (!ReadReg(REG_STATUS, status))
            return Fail(Error::BusError, true);

        Reading result = {};
        result.ready.accel = (status & STATUS_XLDA) != 0;
        result.ready.gyro = (status & STATUS_GDA) != 0;
        result.ready.temperature = (status & STATUS_TDA) != 0;
        if (!result.ready.accel && !result.ready.gyro && !result.ready.temperature)
        {
            *out = result;
            ClearError();
            return true;
        }

        // 0x20..0x2D 共 14 字节：温度、陀螺仪和加速度；BDU 保证各 16 位量在连续读期间一致。
        uint8_t data[14] = {};
        if (!ReadRegs(REG_OUT_TEMP_L, data, sizeof(data)))
            return Fail(Error::BusError, true);

        float accel_scale = 0.0f;
        float gyro_scale = 0.0f;
        uint8_t unused_ctrl1 = 0;
        uint8_t unused_ctrl2 = 0;
        uint8_t unused_ctrl6 = 0;
        uint8_t unused_ctrl8 = 0;
        if (!EncodeConfig(s_config, unused_ctrl1, unused_ctrl2, unused_ctrl6, unused_ctrl8,
                          accel_scale, gyro_scale))
            return Fail(Error::InvalidConfig, true);

        result.temperatureRaw = I16Le(data[0], data[1]);
        result.gxRaw = I16Le(data[2], data[3]);
        result.gyRaw = I16Le(data[4], data[5]);
        result.gzRaw = I16Le(data[6], data[7]);
        result.axRaw = I16Le(data[8], data[9]);
        result.ayRaw = I16Le(data[10], data[11]);
        result.azRaw = I16Le(data[12], data[13]);

        result.axG = result.axRaw * accel_scale;
        result.ayG = result.ayRaw * accel_scale;
        result.azG = result.azRaw * accel_scale;
        result.gxDps = result.gxRaw * gyro_scale;
        result.gyDps = result.gyRaw * gyro_scale;
        result.gzDps = result.gzRaw * gyro_scale;
        // LSM6DSV 温度灵敏度为 256 LSB/°C，25 °C 时典型输出为 0 LSB。
        result.temperatureC = 25.0f + (float)result.temperatureRaw / 256.0f;
        *out = result;
        ClearError();
        return true;
    }

    bool ConfigureFifo(const FifoConfig &config)
    {
        if (!s_initialized || !s_ready)
            return Fail(Error::NotInitialized, false);
        return ApplyFifoConfig(config, true);
    }

    bool GetFifoConfig(FifoConfig *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        if (!s_initialized)
            return Fail(Error::NotInitialized, false);
        *out = s_fifo_config;
        ClearError();
        return true;
    }

    bool ReadFifoStatus(FifoStatus *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        if (!s_initialized || !s_ready)
            return Fail(Error::NotInitialized, false);
        if (s_powered_down)
            return Fail(Error::PoweredDown, false);
        if (!s_fifo_config.enabled)
            return Fail(Error::FifoDisabled, false);

        uint8_t status[2] = {};
        if (!ReadRegs(REG_FIFO_STATUS1, status, sizeof(status)))
            return Fail(Error::BusError, true);
        out->unreadWords = (uint16_t)status[0] | (uint16_t)((status[1] & FIFO_STATUS_DIFF_HIGH) << 8);
        out->watermark = (status[1] & FIFO_STATUS_WTM) != 0;
        out->overrun = (status[1] & FIFO_STATUS_OVR) != 0;
        out->full = (status[1] & FIFO_STATUS_FULL) != 0;
        out->overrunLatched = (status[1] & FIFO_STATUS_OVR_LATCHED) != 0;
        ClearError();
        return true;
    }

    bool ReadFifoWord(FifoWord *out)
    {
        if (!out)
            return Fail(Error::InvalidArgument, false);
        if (!s_initialized || !s_ready)
            return Fail(Error::NotInitialized, false);
        if (s_powered_down)
            return Fail(Error::PoweredDown, false);
        if (!s_fifo_config.enabled)
            return Fail(Error::FifoDisabled, false);

        FifoStatus status = {};
        if (!ReadFifoStatus(&status))
            return false;
        if (status.unreadWords == 0)
            return Fail(Error::FifoEmpty, false);

        uint8_t data[7] = {};
        if (!ReadRegs(REG_FIFO_DATA_TAG, data, sizeof(data)))
            return Fail(Error::BusError, true);

        const uint8_t tag = (uint8_t)(data[0] >> 3);
        out->tagValue = tag;
        out->tag = DecodeFifoTag(tag);
        out->counter = (uint8_t)((data[0] >> 1) & 0x03);
        out->x = I16Le(data[1], data[2]);
        out->y = I16Le(data[3], data[4]);
        out->z = I16Le(data[5], data[6]);
        ClearError();
        return true;
    }

    bool ResetFifo()
    {
        if (!s_initialized || !s_ready)
            return Fail(Error::NotInitialized, false);
        const FifoConfig saved = s_fifo_config;
        const FifoConfig bypass = {};
        if (!ApplyFifoConfig(bypass, false))
            return false;
        if (saved.enabled && !ApplyFifoConfig(saved, false))
            return false;
        ClearError();
        return true;
    }
}
