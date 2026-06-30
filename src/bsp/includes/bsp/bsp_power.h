/*
【模块职责】板级电源与简单 GPIO 控制。管理背光、功放使能、电池 ADC 和充电检测这些直接连到主板引脚的能力。
*/
#pragma once
#include <Arduino.h>

namespace BSP::Power
{
    // 【函数说明】初始化背光和音频功放使能脚，并切到默认开启状态。
    void BeginRails();

    // 【函数说明】直接控制屏幕背光开关；本函数不做亮度调光。
    void SetBacklight(bool on);

    // 【函数说明】直接控制音频功放使能脚；音频播放策略仍由 sys_audio 管理。
    void SetAudioAmp(bool on);

    // 【函数说明】进入 Light Sleep 前保持背光和功放 GPIO 电平，避免睡眠过程中电平漂移。
    void HoldBacklightAndAudio();

    // 【函数说明】唤醒后解除 GPIO hold，让后续 digitalWrite 可以重新控制背光和功放。
    void ReleaseBacklightAndAudio();

    // 【函数说明】初始化电池 ADC 和充电检测输入脚。
    void BeginBatterySense();

    // 【函数说明】读取充电状态。当前充电检测脚为低电平表示正在充电。
    bool IsCharging();

    // 【函数说明】读取 BAT_ADC 引脚上的毫伏值，这是分压后的 ADC 端电压。
    int ReadBatteryAdcMilliVolts();

    // 【函数说明】换算电池实际毫伏值。当前硬件使用 1:1 分压，所以 ADC 读数乘以 2。
    int ReadBatteryMilliVolts();
}
