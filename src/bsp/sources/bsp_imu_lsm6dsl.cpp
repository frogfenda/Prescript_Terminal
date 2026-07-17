/*
【模块职责】实现 LSM6DSL 的 I2C 基础驱动。寄存器、量程灵敏度和温度换算依据
ST LSM6DSL 数据手册 DocID028475 Rev 7。
【实现策略】初始化后开启 BDU 与地址自动递增；每次样本从 STATUS_REG 连续读取到 OUTZ_H_XL，
使温度、陀螺仪和加速度计在一个短 I2C 事务中完成，减少共享 Wire1 的占用次数。
【错误策略】参数错误不会把在线设备标记为离线；总线、身份或寄存器校验错误会清除 ready，
上层可调用 Reset() 显式恢复，驱动不会在高频 Read() 内静默重试或刷串口。
*/
#include "bsp/bsp_imu_lsm6dsl.h"
#include "bsp/bsp_pins.h"

namespace
{
    static constexpr uint8_t REG_WHO_AM_I = 0x0F;
    static constexpr uint8_t REG_CTRL1_XL = 0x10;
    static constexpr uint8_t REG_CTRL2_G = 0x11;
    static constexpr uint8_t REG_CTRL3_C = 0x12;
    static constexpr uint8_t REG_STATUS = 0x1E;

    static constexpr uint8_t WHO_AM_I_VALUE = 0x6A;
    static constexpr uint8_t CTRL3_BDU = 0x40;
    static constexpr uint8_t CTRL3_IF_INC = 0x04;
    static constexpr uint8_t CTRL3_SW_RESET = 0x01;
    static constexpr uint8_t STATUS_TDA = 0x04;
    static constexpr uint8_t STATUS_GDA = 0x02;
    static constexpr uint8_t STATUS_XLDA = 0x01;

    static constexpr uint32_t RESET_TIMEOUT_MS = 100;
    static constexpr uint32_t SENSOR_TURN_ON_MS = 35;

    TwoWire *s_wire = &Wire1;
    uint8_t s_address = 0;
    bool s_initialized = false;
    bool s_ready = false;
    bool s_powered_down = true;
    BSP::Lsm6dsl::Config s_config = {};
    BSP::Lsm6dsl::Error s_last_error = BSP::Lsm6dsl::Error::NotInitialized;

    int16_t I16Le(uint8_t lo, uint8_t hi)
    {
        return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    }

    bool Fail(BSP::Lsm6dsl::Error error, bool mark_not_ready)
    {
        s_last_error = error;
        if (mark_not_ready)
            s_ready = false;
        return false;
    }

    void ClearError()
    {
        s_last_error = BSP::Lsm6dsl::Error::None;
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

        size_t count = s_wire->requestFrom((uint16_t)address, len, true);
        if (count != len)
        {
            // 清空 Wire 接收缓冲，避免一次短读影响共享总线的下一次事务。
            while (s_wire->available())
                s_wire->read();
            return false;
        }

        for (size_t i = 0; i < len; ++i)
            data[i] = (uint8_t)s_wire->read();
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

    bool ReadIdentity(uint8_t address, uint8_t &identity)
    {
        return ReadRegsAt(address, REG_WHO_AM_I, &identity, 1);
    }

    bool ProbeAddress(uint8_t address)
    {
        uint8_t identity = 0;
        return Ack(address) && ReadIdentity(address, identity) && identity == WHO_AM_I_VALUE;
    }

    bool EncodeDataRate(BSP::Lsm6dsl::OutputDataRate rate, uint8_t &bits)
    {
        using BSP::Lsm6dsl::OutputDataRate;
        switch (rate)
        {
        case OutputDataRate::PowerDown:
            bits = 0x00;
            return true;
        case OutputDataRate::Hz12_5:
            bits = 0x10;
            return true;
        case OutputDataRate::Hz26:
            bits = 0x20;
            return true;
        case OutputDataRate::Hz52:
            bits = 0x30;
            return true;
        case OutputDataRate::Hz104:
            bits = 0x40;
            return true;
        case OutputDataRate::Hz208:
            bits = 0x50;
            return true;
        case OutputDataRate::Hz416:
            bits = 0x60;
            return true;
        case OutputDataRate::Hz833:
            bits = 0x70;
            return true;
        case OutputDataRate::Hz1660:
            bits = 0x80;
            return true;
        case OutputDataRate::Hz3330:
            bits = 0x90;
            return true;
        case OutputDataRate::Hz6660:
            bits = 0xA0;
            return true;
        }
        return false;
    }

    bool EncodeAccelRange(BSP::Lsm6dsl::AccelRange range, uint8_t &bits, float &g_per_lsb)
    {
        using BSP::Lsm6dsl::AccelRange;
        switch (range)
        {
        case AccelRange::G2:
            bits = 0x00;
            g_per_lsb = 0.000061f;
            return true;
        case AccelRange::G4:
            bits = 0x08;
            g_per_lsb = 0.000122f;
            return true;
        case AccelRange::G8:
            bits = 0x0C;
            g_per_lsb = 0.000244f;
            return true;
        case AccelRange::G16:
            bits = 0x04;
            g_per_lsb = 0.000488f;
            return true;
        }
        return false;
    }

    bool EncodeGyroRange(BSP::Lsm6dsl::GyroRange range, uint8_t &bits, float &dps_per_lsb)
    {
        using BSP::Lsm6dsl::GyroRange;
        switch (range)
        {
        case GyroRange::Dps125:
            bits = 0x02;
            dps_per_lsb = 0.004375f;
            return true;
        case GyroRange::Dps250:
            bits = 0x00;
            dps_per_lsb = 0.00875f;
            return true;
        case GyroRange::Dps500:
            bits = 0x04;
            dps_per_lsb = 0.01750f;
            return true;
        case GyroRange::Dps1000:
            bits = 0x08;
            dps_per_lsb = 0.035f;
            return true;
        case GyroRange::Dps2000:
            bits = 0x0C;
            dps_per_lsb = 0.070f;
            return true;
        }
        return false;
    }

    bool EncodeConfig(const BSP::Lsm6dsl::Config &config,
                      uint8_t &ctrl1,
                      uint8_t &ctrl2,
                      float &accel_scale,
                      float &gyro_scale)
    {
        uint8_t accel_rate = 0;
        uint8_t gyro_rate = 0;
        uint8_t accel_range = 0;
        uint8_t gyro_range = 0;

        if (!EncodeDataRate(config.accelRate, accel_rate) ||
            !EncodeDataRate(config.gyroRate, gyro_rate) ||
            !EncodeAccelRange(config.accelRange, accel_range, accel_scale) ||
            !EncodeGyroRange(config.gyroRange, gyro_range, gyro_scale))
        {
            return false;
        }

        ctrl1 = accel_rate | accel_range;
        ctrl2 = gyro_rate | gyro_range;
        return true;
    }

    bool WriteAndVerifyConfig(const BSP::Lsm6dsl::Config &config, bool save_config)
    {
        uint8_t ctrl1 = 0;
        uint8_t ctrl2 = 0;
        float unused_accel_scale = 0.0f;
        float unused_gyro_scale = 0.0f;
        if (!EncodeConfig(config, ctrl1, ctrl2, unused_accel_scale, unused_gyro_scale))
            return Fail(BSP::Lsm6dsl::Error::InvalidConfig, false);

        /*
         * CTRL3_C 先单独写入，确保后续 CTRL1_XL/CTRL2_G 的连续事务一定启用 IF_INC。
         * BDU 防止读取 16 位输出期间高低字节被新采样拆开更新。
         */
        if (!WriteReg(REG_CTRL3_C, CTRL3_BDU | CTRL3_IF_INC))
            return Fail(BSP::Lsm6dsl::Error::BusError, true);

        const uint8_t control[2] = {ctrl1, ctrl2};
        if (!WriteRegs(REG_CTRL1_XL, control, sizeof(control)))
            return Fail(BSP::Lsm6dsl::Error::BusError, true);

        uint8_t verify[3] = {};
        if (!ReadRegs(REG_CTRL1_XL, verify, sizeof(verify)))
            return Fail(BSP::Lsm6dsl::Error::BusError, true);
        if (verify[0] != ctrl1 || verify[1] != ctrl2 ||
            (verify[2] & (CTRL3_BDU | CTRL3_IF_INC)) != (CTRL3_BDU | CTRL3_IF_INC))
        {
            return Fail(BSP::Lsm6dsl::Error::RegisterVerifyFailed, true);
        }

        if (save_config)
            s_config = config;

        s_powered_down = config.accelRate == BSP::Lsm6dsl::OutputDataRate::PowerDown &&
                         config.gyroRate == BSP::Lsm6dsl::OutputDataRate::PowerDown;
        if (!s_powered_down)
            delay(SENSOR_TURN_ON_MS);

        s_ready = true;
        ClearError();
        return true;
    }

    bool SoftwareReset()
    {
        if (!WriteReg(REG_CTRL3_C, CTRL3_SW_RESET))
            return Fail(BSP::Lsm6dsl::Error::BusError, true);

        const uint32_t started_ms = millis();
        while (millis() - started_ms < RESET_TIMEOUT_MS)
        {
            uint8_t ctrl3 = 0;
            if (!ReadReg(REG_CTRL3_C, ctrl3))
                return Fail(BSP::Lsm6dsl::Error::BusError, true);
            if ((ctrl3 & CTRL3_SW_RESET) == 0)
            {
                uint8_t identity = 0;
                if (!ReadReg(REG_WHO_AM_I, identity))
                    return Fail(BSP::Lsm6dsl::Error::BusError, true);
                if (identity != WHO_AM_I_VALUE)
                    return Fail(BSP::Lsm6dsl::Error::IdentityMismatch, true);
                return true;
            }
            delay(1);
        }
        return Fail(BSP::Lsm6dsl::Error::ResetTimeout, true);
    }
}

namespace BSP::Lsm6dsl
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
        s_last_error = Error::NotInitialized;

        // 先验证调用者配置，避免发现硬件后才因非法枚举值留下半初始化状态。
        uint8_t unused_ctrl1 = 0;
        uint8_t unused_ctrl2 = 0;
        float unused_accel_scale = 0.0f;
        float unused_gyro_scale = 0.0f;
        if (!EncodeConfig(config,
                          unused_ctrl1,
                          unused_ctrl2,
                          unused_accel_scale,
                          unused_gyro_scale))
        {
            return Fail(Error::InvalidConfig, false);
        }

        if (s_wire == &Wire1)
        {
            // 共享总线沿用 RTC/TM6605 已验证的 100 kHz 和 20 ms 超时，不擅自提高整条总线速率。
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
        if (!SoftwareReset() || !WriteAndVerifyConfig(config, true))
        {
            Serial.printf("[BSP][IMU] LSM6DSL 初始化失败：地址=0x%02X，错误码=%u。\n",
                          s_address,
                          (unsigned)s_last_error);
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
        return SoftwareReset() && WriteAndVerifyConfig(s_config, false);
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
        return WriteAndVerifyConfig(power_down, false);
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
        return WriteAndVerifyConfig(s_config, false);
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

        DataReady result = {};
        result.accel = (status & STATUS_XLDA) != 0;
        result.gyro = (status & STATUS_GDA) != 0;
        result.temperature = (status & STATUS_TDA) != 0;
        *out = result;
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

        /*
         * 0x1E..0x2D 共 16 字节：STATUS、保留字节、温度 2 字节、陀螺仪 6 字节、加速度 6 字节。
         * BDU 保证每个 16 位通道的高低字节一致；状态位让上层自行决定是否消费重复样本。
         */
        uint8_t data[16] = {};
        if (!ReadRegs(REG_STATUS, data, sizeof(data)))
            return Fail(Error::BusError, true);

        float accel_scale = 0.0f;
        float gyro_scale = 0.0f;
        uint8_t unused_accel_bits = 0;
        uint8_t unused_gyro_bits = 0;
        if (!EncodeAccelRange(s_config.accelRange, unused_accel_bits, accel_scale) ||
            !EncodeGyroRange(s_config.gyroRange, unused_gyro_bits, gyro_scale))
        {
            return Fail(Error::InvalidConfig, true);
        }

        Reading result = {};
        result.ready.accel = (data[0] & STATUS_XLDA) != 0;
        result.ready.gyro = (data[0] & STATUS_GDA) != 0;
        result.ready.temperature = (data[0] & STATUS_TDA) != 0;

        result.temperatureRaw = I16Le(data[2], data[3]);
        result.gxRaw = I16Le(data[4], data[5]);
        result.gyRaw = I16Le(data[6], data[7]);
        result.gzRaw = I16Le(data[8], data[9]);
        result.axRaw = I16Le(data[10], data[11]);
        result.ayRaw = I16Le(data[12], data[13]);
        result.azRaw = I16Le(data[14], data[15]);

        result.axG = result.axRaw * accel_scale;
        result.ayG = result.ayRaw * accel_scale;
        result.azG = result.azRaw * accel_scale;
        result.gxDps = result.gxRaw * gyro_scale;
        result.gyDps = result.gyRaw * gyro_scale;
        result.gzDps = result.gzRaw * gyro_scale;

        // 手册给出 25 °C 时典型输出 0 LSB、灵敏度 256 LSB/°C；该温度主要用于芯片状态和零偏补偿。
        result.temperatureC = 25.0f + (float)result.temperatureRaw / 256.0f;

        *out = result;
        ClearError();
        return true;
    }
}
