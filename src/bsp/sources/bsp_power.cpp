/*
【模块职责】板级电源与简单 GPIO 控制实现。所有背光、功放、电池检测引脚的直接读写都收束在这里。
*/
#include "bsp/bsp_power.h"
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"

namespace BSP::Power
{
    namespace
    {
        constexpr int BAT_ADC_TOP_OHMS = 20000;
        constexpr int BAT_ADC_BOTTOM_OHMS = 20000;
    }

    // 【函数说明】初始化背光和功放使能脚，并设置成当前固件默认的“可显示、可发声”状态。
    void BeginRails()
    {
        // 背光和功放都是简单 GPIO 输出，默认开机后直接拉到启用状态。
        pinMode(Pins::BACKLIGHT, OUTPUT);
        pinMode(Pins::AUDIO_SD, OUTPUT);
        SetAudioAmp(true);
        SetBacklight(true);
        Serial.println("[BSP][电源] 背光与功放使能引脚已初始化。");
    }

    // 【函数说明】设置屏幕背光开关。当前硬件为高电平点亮。
    void SetBacklight(bool on)
    {
        gpio_num_t pin = (gpio_num_t)Pins::BACKLIGHT;
        gpio_hold_dis(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, on ? 1 : 0);
    }

    // 【函数说明】设置音频功放使能。当前硬件为高电平启用。
    void SetAudioAmp(bool on)
    {
        gpio_num_t pin = (gpio_num_t)Pins::AUDIO_SD;
        gpio_hold_dis(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, on ? 1 : 0);
    }

    // 【函数说明】Light Sleep 前锁住背光和功放 GPIO 电平，避免睡眠过程中引脚浮动。
    void HoldBacklightAndAudio()
    {
        // ESP32 的 gpio hold 只保持当前输出电平，不负责决定电平应该是什么。
        gpio_hold_en((gpio_num_t)Pins::BACKLIGHT);
        gpio_hold_en((gpio_num_t)Pins::AUDIO_SD);
        gpio_deep_sleep_hold_en();
    }

    // 【函数说明】唤醒后解除背光和功放 GPIO hold，让 HAL 可以重新按顺序点亮外设。
    void ReleaseBacklightAndAudio()
    {
        gpio_hold_dis((gpio_num_t)Pins::BACKLIGHT);
        gpio_hold_dis((gpio_num_t)Pins::AUDIO_SD);
    }

    // 【函数说明】初始化电池检测相关引脚。
    void BeginBatterySense()
    {
        // 新版原理图：VIN 经过 20k/20k 分压到 BAT_ADC；BQ24075 CHG# 经 CHAG 网到 MCU，外部 1k 上拉，低电平表示正在充电。
        pinMode(Pins::BAT_ADC, ANALOG);
        pinMode(Pins::CHRG, INPUT);
        Serial.println("[BSP][电源] 电池 ADC 与充电检测引脚已初始化。");
    }

    // 【函数说明】读取充电检测脚，返回当前是否处于充电状态。
    bool IsCharging()
    {
        return digitalRead(Pins::CHRG) == LOW;
    }

    // 【函数说明】读取 ADC 引脚端的毫伏值。这里还不是电池真实电压。
    int ReadBatteryAdcMilliVolts()
    {
        return analogReadMilliVolts(Pins::BAT_ADC);
    }

    // 【函数说明】把 ADC 端电压换算成电池端电压。
    int ReadBatteryMilliVolts()
    {
        int adc_mv = ReadBatteryAdcMilliVolts();
        return adc_mv * (BAT_ADC_TOP_OHMS + BAT_ADC_BOTTOM_OHMS) / BAT_ADC_BOTTOM_OHMS;
    }
}

