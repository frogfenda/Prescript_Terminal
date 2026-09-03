/*
【模块职责】板级电源与简单 GPIO 控制实现。所有背光、功放、电池检测引脚的直接读写都收束在这里。
*/
#include "bsp/bsp_power.h"
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"

namespace BSP::Power
{
    // 【函数说明】初始化背光和功放使能脚，并设置成当前固件默认的“可显示、可发声”状态。
    void BeginRails()
    {
        /*
         * GPIO7 接 NS4168 CTRL：低电平持续超过 100us 进入关断，高电平退出关断并选择右声道。
         * SYS 混音器把单声道复制到左右两个 I2S 时隙，因此选择右声道不会丢失音频。
         */
        pinMode(Pins::BACKLIGHT, OUTPUT);
        pinMode(Pins::AUDIO_AMP_CTRL, OUTPUT);
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

    // 【函数说明】设置 NS4168 控制脚；高电平工作、低电平关断。
    void SetAudioAmp(bool on)
    {
        gpio_num_t pin = (gpio_num_t)Pins::AUDIO_AMP_CTRL;
        gpio_hold_dis(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, on ? 1 : 0);
    }

    // 【函数说明】Light Sleep 前锁住背光和功放 GPIO 电平，避免睡眠过程中引脚浮动。
    void HoldBacklightAndAudio()
    {
        // ESP32 的 gpio hold 只保持当前输出电平，不负责决定电平应该是什么。
        gpio_hold_en((gpio_num_t)Pins::BACKLIGHT);
        gpio_hold_en((gpio_num_t)Pins::AUDIO_AMP_CTRL);
        gpio_deep_sleep_hold_en();
    }

    // 【函数说明】唤醒后解除背光和功放 GPIO hold，让 HAL 可以重新按顺序点亮外设。
    void ReleaseBacklightAndAudio()
    {
        gpio_hold_dis((gpio_num_t)Pins::BACKLIGHT);
        gpio_hold_dis((gpio_num_t)Pins::AUDIO_AMP_CTRL);
    }

    // 【函数说明】初始化电池检测相关引脚。
    void BeginBatterySense()
    {
        /*
         * 当前原理图使用 1M/1M 分压与 GPIO1 CHAG。分压比例仍是 1:1；GPIO39 已专用于
         * W25N01 MOSI，电源代码不能再把它配置成充电输入。高阻分压的 ADC 稳定性仍需实板验证。
         */
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
        return adc_mv * (Pins::BAT_DIVIDER_TOP_OHMS + Pins::BAT_DIVIDER_BOTTOM_OHMS) /
               Pins::BAT_DIVIDER_BOTTOM_OHMS;
    }
}

