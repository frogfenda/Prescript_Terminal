#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace BSP::Lsm6dsl
{
    static constexpr uint8_t ADDRESS_LOW = 0x6A;
    static constexpr uint8_t ADDRESS_HIGH = 0x6B;

    struct Reading
    {
        int16_t axRaw = 0;
        int16_t ayRaw = 0;
        int16_t azRaw = 0;
        int16_t gxRaw = 0;
        int16_t gyRaw = 0;
        int16_t gzRaw = 0;
        float axG = 0.0f;
        float ayG = 0.0f;
        float azG = 0.0f;
        float gxDps = 0.0f;
        float gyDps = 0.0f;
        float gzDps = 0.0f;
    };

    bool Begin(TwoWire &wire = Wire, uint8_t address = 0);
    bool IsReady();
    bool IsPresent(uint8_t address = 0);
    uint8_t Address();
    bool Read(Reading *out);
}
