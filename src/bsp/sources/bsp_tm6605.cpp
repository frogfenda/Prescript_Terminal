#include "bsp/bsp_tm6605.h"
#include "bsp/bsp_pins.h"

namespace
{
    TwoWire *s_wire = &Wire1;
    uint8_t s_address = BSP::Tm6605::DEFAULT_ADDRESS;
    bool s_ready = false;
    bool s_sleeping = false;
    uint32_t s_next_ready_ms = 0;

    constexpr BSP::Tm6605::EffectInfo EFFECTS[] = {
        {1, 65},
        {4, 45},
        {7, 130},
        {10, 200},
        {13, 215},
        {14, 190},
        {15, 730},
        {17, 90},
        {21, 65},
        {24, 20},
        {27, 120},
        {31, 120},
        {34, 100},
        {37, 150},
        {41, 150},
        {44, 150},
        {47, 240},
        {58, 620},
        {70, 390},
        {71, 620},
        {72, 400},
        {73, 650},
        {74, 410},
        {75, 490},
        {76, 340},
        {77, 390},
        {78, 310},
        {79, 360},
        {80, 340},
        {81, 350},
        {82, 320},
        {83, 650},
        {84, 310},
        {85, 640},
        {86, 320},
        {87, 460},
        {88, 290},
        {89, 615},
        {90, 320},
        {91, 590},
        {92, 330},
        {93, 470},
        {118, 10000},
        {119, 480},
        {BSP::Tm6605::EFFECT_SLEEP, 0},
    };

    bool Ack(uint8_t address)
    {
        if (!s_wire || address == 0)
            return false;

        s_wire->beginTransmission(address);
        return s_wire->endTransmission() == 0;
    }

    uint8_t ClampIntensity(uint8_t intensity)
    {
        if (intensity <= 1)
            return 1;
        if (intensity >= 3)
            return 3;
        return 2;
    }
}

namespace BSP::Tm6605
{
    bool Begin(TwoWire &wire, uint8_t address)
    {
        s_wire = &wire;
        s_address = address == 0 ? DEFAULT_ADDRESS : address;
        s_ready = false;
        s_sleeping = false;
        s_next_ready_ms = 0;

        if (s_wire == &Wire1)
        {
            s_wire->begin(Pins::I2C_SDA, Pins::I2C_SCL);
            s_wire->setTimeOut(20);
        }

        s_ready = Ack(s_address);
        if (s_ready)
        {
            Serial.printf("[BSP][TM6605] detected at 7-bit 0x%02X (datasheet write byte 0x%02X).\n",
                          s_address,
                          DATASHEET_WRITE_ADDRESS);
            SetPlaybackEnabled(true);
        }
        else
        {
            Serial.printf("[BSP][TM6605] not detected at 7-bit 0x%02X (datasheet write byte 0x%02X).\n",
                          s_address,
                          DATASHEET_WRITE_ADDRESS);
        }
        return s_ready;
    }

    bool IsReady()
    {
        return s_ready;
    }

    bool IsPresent(uint8_t address)
    {
        return Ack(address == 0 ? DEFAULT_ADDRESS : address);
    }

    uint8_t Address()
    {
        return s_address;
    }

    TwoWire *Bus()
    {
        return s_wire;
    }

    bool IsBusy()
    {
        return (int32_t)(s_next_ready_ms - millis()) > 0;
    }

    const EffectInfo *Effects(size_t *count)
    {
        if (count)
            *count = sizeof(EFFECTS) / sizeof(EFFECTS[0]);
        return EFFECTS;
    }

    uint16_t EffectDurationMs(uint8_t effect)
    {
        for (const auto &item : EFFECTS)
        {
            if (item.id == effect)
                return item.durationMs;
        }
        return 0;
    }

    Effect ResolveEffect(SemanticEffect effect, uint8_t intensity)
    {
        const uint8_t level = ClampIntensity(intensity);

        switch (effect)
        {
        case SemanticEffect::Tick:
            // 旋钮反馈优先短促，强度只增加触击感，不拖长时间。
            return level == 1 ? Flash : (level == 2 ? SharpClick : StrongClick);

        case SemanticEffect::Confirm:
        case SemanticEffect::CoinHeads:
            return level == 1 ? Flash : (level == 2 ? SharpClick : StrongClick);

        case SemanticEffect::Back:
            return level == 1 ? ShortDoubleFlash : (level == 2 ? ShortDoubleMedium : ShortDoubleStrong);

        case SemanticEffect::CoinTails:
            return level == 1 ? LightBump : (level == 2 ? MediumClick : SharpClick);

        case SemanticEffect::Alert:
            return level == 1 ? Alert : (level == 2 ? StrongAlert : MediumAlert);

        case SemanticEffect::DecodeDone:
            // TM6605 无多段效果队列，先用单个带段落感的效果替代解码完成连击。
            return level == 1 ? ShortDoubleMedium : (level == 2 ? ShortDoubleStrong : TransitionClick);
        }

        return SharpClick;
    }

    uint8_t ResolveEffectId(SemanticEffect effect, uint8_t intensity)
    {
        return static_cast<uint8_t>(ResolveEffect(effect, intensity));
    }

    bool WriteReg(uint8_t reg, uint8_t value)
    {
        if (!s_wire || s_address == 0)
            return false;

        s_wire->beginTransmission(s_address);
        s_wire->write(reg);
        s_wire->write(value);

        uint8_t err = s_wire->endTransmission();
        s_ready = err == 0;
        if (!s_ready)
            Serial.printf("[BSP][TM6605] write failed: reg=0x%02X value=0x%02X err=%u.\n", reg, value, err);
        return s_ready;
    }

    bool SetPlaybackEnabled(bool enabled)
    {
        return WriteReg(REG_PLAY_CONTROL, enabled ? PLAY_ENABLE : PLAY_DISABLE);
    }

    bool PlayEffect(uint8_t effect, bool force)
    {
        if (!s_ready || effect == 0)
            return false;

        if (s_sleeping)
            Wakeup();

        if (!force && IsBusy())
            return false;

        bool ok = true;
        ok &= WriteReg(REG_EFFECT_SELECT, effect);
        ok &= SetPlaybackEnabled(true);

        if (ok)
        {
            const uint16_t duration = EffectDurationMs(effect);
            s_next_ready_ms = duration ? millis() + duration + 8 : 0;
            s_sleeping = effect == EFFECT_SLEEP;
        }
        return ok;
    }

    bool PlayEffect(Effect effect, bool force)
    {
        return PlayEffect(static_cast<uint8_t>(effect), force);
    }

    bool PlaySemantic(SemanticEffect effect, uint8_t intensity, bool force)
    {
        return PlayEffect(ResolveEffect(effect, intensity), force);
    }

    bool Stop()
    {
        s_next_ready_ms = 0;
        return SetPlaybackEnabled(false);
    }

    void Sleep()
    {
        PlayEffect(EFFECT_SLEEP, true);
        s_sleeping = true;
    }

    void Wakeup()
    {
        if (!s_ready)
            return;

        // 手册说明睡眠后任意 I2C 指令可唤醒，但该指令可能被丢弃；这里用播放控制写入做空唤醒。
        SetPlaybackEnabled(true);
        delay(2);
        s_sleeping = false;
        s_next_ready_ms = 0;
    }
}
