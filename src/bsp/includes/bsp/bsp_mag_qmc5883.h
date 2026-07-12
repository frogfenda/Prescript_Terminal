#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace BSP::Qmc5883
{
    static constexpr uint8_t ADDRESS_QMC5883L = 0x0D;
    static constexpr uint8_t ADDRESS_QMC5883P = 0x2C;

    enum class Type : uint8_t
    {
        None,
        Qmc5883l,
        Qmc5883p,
    };

    struct Reading
    {
        int16_t xRaw = 0;
        int16_t yRaw = 0;
        int16_t zRaw = 0;
    };

    bool Begin(TwoWire &wire = Wire, uint8_t address = 0);
    bool IsReady();
    bool IsPresent(uint8_t address = 0);
    uint8_t Address();
    Type SensorType();
    const char *TypeName(Type type);
    const char *TypeName();
    bool ReadRaw(Reading *out);
}
