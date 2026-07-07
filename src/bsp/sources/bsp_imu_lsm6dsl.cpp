#include "bsp/bsp_imu_lsm6dsl.h"

namespace
{
    static constexpr uint8_t REG_WHO_AM_I = 0x0F;
    static constexpr uint8_t REG_CTRL1_XL = 0x10;
    static constexpr uint8_t REG_CTRL2_G = 0x11;
    static constexpr uint8_t REG_CTRL3_C = 0x12;
    static constexpr uint8_t REG_OUTX_L_G = 0x22;
    static constexpr uint8_t WHO_AM_I_VALUE = 0x6A;

    TwoWire *s_wire = &Wire;
    uint8_t s_address = 0;
    bool s_ready = false;

    int16_t I16Le(uint8_t lo, uint8_t hi)
    {
        return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    }

    bool ReadRegs(uint8_t reg, uint8_t *data, size_t len)
    {
        if (!s_wire || !data || len == 0 || s_address == 0)
            return false;

        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        if (s_wire->endTransmission(false) != 0)
            return false;

        size_t count = s_wire->requestFrom((uint16_t)s_address, len, true);
        if (count != len)
        {
            while (s_wire->available())
                s_wire->read();
            return false;
        }

        for (size_t i = 0; i < len; ++i)
            data[i] = (uint8_t)s_wire->read();

        return true;
    }

    bool ReadReg(uint8_t reg, uint8_t &value)
    {
        return ReadRegs(reg, &value, 1);
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
        if (!s_wire || address == 0)
            return false;

        uint8_t previous = s_address;
        s_address = address;
        uint8_t who = 0;
        bool ok = ReadReg(REG_WHO_AM_I, who) && who == WHO_AM_I_VALUE;
        s_address = previous;
        return ok;
    }
}

namespace BSP::Lsm6dsl
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        s_wire = &wire;
        s_address = 0;
        s_ready = false;

        if (address != 0)
        {
            if (!ProbeAddress(address))
                return false;
            s_address = address;
        }
        else if (ProbeAddress(ADDRESS_LOW))
        {
            s_address = ADDRESS_LOW;
        }
        else if (ProbeAddress(ADDRESS_HIGH))
        {
            s_address = ADDRESS_HIGH;
        }
        else
        {
            return false;
        }

        bool ok = true;
        ok &= WriteReg(REG_CTRL3_C, 0x44); // BDU + register auto-increment.
        ok &= WriteReg(REG_CTRL1_XL, 0x40); // 104 Hz, +/-2 g.
        ok &= WriteReg(REG_CTRL2_G, 0x40); // 104 Hz, +/-245 dps.
        delay(20);

        s_ready = ok;
        return ok;
    }

    bool IsReady()
    {
        return s_ready;
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

    bool Read(Reading *out)
    {
        if (!out || !s_ready || s_address == 0)
            return false;

        uint8_t data[12] = {};
        if (!ReadRegs(REG_OUTX_L_G, data, sizeof(data)))
        {
            s_ready = false;
            return false;
        }

        out->gxRaw = I16Le(data[0], data[1]);
        out->gyRaw = I16Le(data[2], data[3]);
        out->gzRaw = I16Le(data[4], data[5]);
        out->axRaw = I16Le(data[6], data[7]);
        out->ayRaw = I16Le(data[8], data[9]);
        out->azRaw = I16Le(data[10], data[11]);

        out->axG = out->axRaw * 0.000061f;
        out->ayG = out->ayRaw * 0.000061f;
        out->azG = out->azRaw * 0.000061f;
        out->gxDps = out->gxRaw * 0.00875f;
        out->gyDps = out->gyRaw * 0.00875f;
        out->gzDps = out->gzRaw * 0.00875f;
        return true;
    }
}
