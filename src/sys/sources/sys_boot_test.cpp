#include "sys/sys_boot_test.h"

#include <Arduino.h>
#include <Wire.h>

#include "bsp/bsp_pins.h"
#include "bsp/bsp_tm6605.h"
#include "sys/sys_constants.h"

#ifndef PRESCRIPT_TM6605_BOOT_TEST
#define PRESCRIPT_TM6605_BOOT_TEST 0
#endif

namespace
{
    uint32_t s_tm6605LastPlayMs = 0;
    uint8_t s_tm6605EffectIndex = 0;

    constexpr BSP::Tm6605::Effect TEST_EFFECTS[] = {
        BSP::Tm6605::Effect::SharpClick,
        BSP::Tm6605::Effect::LightBump,
        BSP::Tm6605::Effect::DoubleClick,
        BSP::Tm6605::Effect::MediumAlert,
    };

    const char *EffectName(uint8_t effect)
    {
        switch (effect)
        {
        case BSP::Tm6605::Effect::SharpClick:
            return "SharpClick";
        case BSP::Tm6605::Effect::LightBump:
            return "LightBump";
        case BSP::Tm6605::Effect::DoubleClick:
            return "DoubleClick";
        case BSP::Tm6605::Effect::MediumAlert:
            return "MediumAlert";
        default:
            return "Unknown";
        }
    }

    const char *I2CDeviceName(uint8_t address)
    {
        switch (address)
        {
        case 0x2C:
            return "QMC5883P";
        case BSP::Tm6605::DEFAULT_ADDRESS:
            return "TM6605";
        case 0x51:
            return "PCF8563";
        case 0x6A:
            return "LSM6DSL";
        default:
            return "unknown";
        }
    }

    void ScanI2C()
    {
        uint8_t found = 0;
        Serial.printf("[I2C] scan begin: SCL=%d SDA=%d\n", BSP::Pins::I2C_SCL, BSP::Pins::I2C_SDA);

        for (uint8_t address = 0x08; address <= 0x77; ++address)
        {
            Wire1.beginTransmission(address);
            uint8_t err = Wire1.endTransmission();
            if (err == 0)
            {
                ++found;
                Serial.printf("[I2C] found 0x%02X %s\n", address, I2CDeviceName(address));
            }
            delay(2);
        }

        Serial.printf("[I2C] scan done, found %u device(s).\n", found);
    }

    void PlayNextTm6605Effect(bool force)
    {
        const uint8_t effect = static_cast<uint8_t>(TEST_EFFECTS[s_tm6605EffectIndex]);
        const uint16_t duration = BSP::Tm6605::EffectDurationMs(effect);

        Serial.printf("[TM6605] addr=0x%02X command: 0x%02X=0x%02X, 0x%02X=0x%02X (%s, %ums)\n",
                      BSP::Tm6605::Address(),
                      BSP::Tm6605::REG_EFFECT_SELECT,
                      effect,
                      BSP::Tm6605::REG_PLAY_CONTROL,
                      BSP::Tm6605::PLAY_ENABLE,
                      EffectName(effect),
                      duration);

        bool ok = BSP::Tm6605::PlayEffect(effect, force);
        Serial.println(ok ? "[TM6605] play ok." : "[TM6605] play failed/busy.");

        s_tm6605EffectIndex = (s_tm6605EffectIndex + 1) % (sizeof(TEST_EFFECTS) / sizeof(TEST_EFFECTS[0]));
        s_tm6605LastPlayMs = millis();
    }
}

namespace SysBootTest
{
    bool Enabled()
    {
        return PRESCRIPT_TM6605_BOOT_TEST != 0;
    }

    void Setup()
    {
        setCpuFrequencyMhz(PrescriptConst::CPU_RUNTIME_MHZ);

        Serial.begin(115200);
        delay(500);
        Serial.println("\n=== TM6605 Boot Test ===");
        Serial.println("[Main] APP startup is disabled in this test build.");

        Wire1.begin(BSP::Pins::I2C_SDA, BSP::Pins::I2C_SCL);
        Wire1.setClock(100000);
        Wire1.setTimeOut(20);

        ScanI2C();

        if (!BSP::Tm6605::Begin(Wire1, BSP::Tm6605::DEFAULT_ADDRESS))
        {
            Serial.println("[TM6605] not ready, commands will not be sent.");
            return;
        }

        Serial.println("[TM6605] ready, sending first effect.");
        PlayNextTm6605Effect(true);
    }

    void Loop()
    {
        if (!BSP::Tm6605::IsReady())
        {
            delay(1000);
            return;
        }

        if (millis() - s_tm6605LastPlayMs >= 2000)
            PlayNextTm6605Effect(false);

        delay(10);
    }
}

