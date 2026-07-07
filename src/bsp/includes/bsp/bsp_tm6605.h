/*
【模块职责】TM6605 LRA 触觉驱动 BSP。负责 I2C 初始化、效果号播放和休眠唤醒。
【硬件说明】TM6605 只支持 I2C 写命令；手册中的 0x5A 是 8-bit 写地址，Wire 使用 7-bit 地址 0x2D。
*/
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace BSP::Tm6605
{
    static constexpr uint8_t DATASHEET_WRITE_ADDRESS = 0x5A;
    static constexpr uint8_t DEFAULT_ADDRESS = DATASHEET_WRITE_ADDRESS >> 1;
    static constexpr uint8_t REG_EFFECT_SELECT = 0x04;
    static constexpr uint8_t REG_PLAY_CONTROL = 0x0C;
    static constexpr uint8_t PLAY_DISABLE = 0x00;
    static constexpr uint8_t PLAY_ENABLE = 0x01;
    static constexpr uint8_t EFFECT_SLEEP = 123;

    struct EffectInfo
    {
        uint8_t id;
        uint16_t durationMs;
    };

    enum Effect : uint8_t
    {
        StrongClick = 1,
        SharpClick = 4,
        LightBump = 7,
        DoubleClick = 10,
        LightPulse = 13,
        StrongAlert = 14,
        MediumAlert = 15,
        StrongClick2 = 17,
        MediumClick = 21,
        Flash = 24,
        ShortDoubleStrong = 27,
        ShortDoubleMedium = 31,
        ShortDoubleFlash = 34,
        LongDoubleSharp = 37,
        LongDoubleMedium = 41,
        LongDoubleFlash = 44,
        Alert = 47,
        TransitionClick = 58,
        LongAlert = 118,
        SoftNoise = 119,
        SleepCommand = EFFECT_SLEEP,
    };

    enum class SemanticEffect : uint8_t
    {
        Tick,
        Confirm,
        Back,
        CoinHeads,
        CoinTails,
        Alert,
        DecodeDone,
    };

    bool Begin(TwoWire &wire = Wire1, uint8_t address = DEFAULT_ADDRESS);
    bool IsReady();
    bool IsPresent(uint8_t address = DEFAULT_ADDRESS);
    uint8_t Address();
    TwoWire *Bus();
    bool IsBusy();

    const EffectInfo *Effects(size_t *count = nullptr);
    uint16_t EffectDurationMs(uint8_t effect);
    Effect ResolveEffect(SemanticEffect effect, uint8_t intensity);
    uint8_t ResolveEffectId(SemanticEffect effect, uint8_t intensity);

    bool WriteReg(uint8_t reg, uint8_t value);
    bool SetPlaybackEnabled(bool enabled);
    bool PlayEffect(uint8_t effect, bool force = false);
    bool PlayEffect(Effect effect, bool force = false);
    bool PlaySemantic(SemanticEffect effect, uint8_t intensity, bool force = false);
    bool Stop();
    void Sleep();
    void Wakeup();
}
