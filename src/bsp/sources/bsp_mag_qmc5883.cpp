#include "bsp/bsp_mag_qmc5883.h"

namespace
{
    TwoWire *s_wire = &Wire;
    uint8_t s_address = 0;
    BSP::Qmc5883::Type s_type = BSP::Qmc5883::Type::None;
    bool s_ready = false;

    int16_t I16Le(uint8_t lo, uint8_t hi)
    {
        return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    }

    bool Ack(uint8_t address)
    {
        if (!s_wire || address == 0)
            return false;

        s_wire->beginTransmission(address);
        return s_wire->endTransmission() == 0;
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

    bool WriteReg(uint8_t reg, uint8_t value)
    {
        if (!s_wire || s_address == 0)
            return false;

        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        s_wire->write(value);
        return s_wire->endTransmission() == 0;
    }
}

namespace BSP::Qmc5883
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        s_wire = &wire;
        s_address = 0;
        s_type = Type::None;
        s_ready = false;

        if (address == ADDRESS_QMC5883P || (address == 0 && Ack(ADDRESS_QMC5883P)))
        {
            s_address = ADDRESS_QMC5883P;
            s_type = Type::Qmc5883p;
        }
        else if (address == ADDRESS_QMC5883L || (address == 0 && Ack(ADDRESS_QMC5883L)))
        {
            s_address = ADDRESS_QMC5883L;
            s_type = Type::Qmc5883l;
        }
        else
        {
            return false;
        }

        if (!Ack(s_address))
        {
            s_address = 0;
            s_type = Type::None;
            return false;
        }

        bool ok = true;
        if (s_type == Type::Qmc5883p)
        {
            ok &= WriteReg(0x0B, 0x00); // CTRL2: normal operation.
            ok &= WriteReg(0x0A, 0x1D); // CTRL1: continuous, 200 Hz, 8 G, OSR 512.
        }
        else
        {
            ok &= WriteReg(0x0B, 0x01); // SET/RESET period.
            ok &= WriteReg(0x0A, 0x00); // No interrupt/rollover special mode.
            ok &= WriteReg(0x09, 0x1D); // Continuous, 200 Hz, 8 G, OSR 512.
        }
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
            return Ack(address);
        return Ack(ADDRESS_QMC5883P) || Ack(ADDRESS_QMC5883L);
    }

    uint8_t Address()
    {
        return s_address;
    }

    Type SensorType()
    {
        return s_type;
    }

    const char *TypeName(Type type)
    {
        switch (type)
        {
        case Type::Qmc5883p:
            return "QMC5883P";
        case Type::Qmc5883l:
            return "QMC5883L";
        default:
            return "QMC5883";
        }
    }

    const char *TypeName()
    {
        return TypeName(s_type);
    }

    bool ReadRaw(Reading *out)
    {
        if (!out || !s_ready || s_address == 0)
            return false;

        uint8_t data[6] = {};
        uint8_t dataReg = (s_type == Type::Qmc5883p) ? 0x01 : 0x00;
        if (!ReadRegs(dataReg, data, sizeof(data)))
        {
            s_ready = false;
            return false;
        }

        out->xRaw = I16Le(data[0], data[1]);
        out->yRaw = I16Le(data[2], data[3]);
        out->zRaw = I16Le(data[4], data[5]);
        return true;
    }
}
