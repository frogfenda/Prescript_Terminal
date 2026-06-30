/*
【模块职责】板级引脚定义。集中描述当前 ESP32-S3 N16R8 主板连接到屏幕、音频、震动、NFC、电源检测和输入件的物理引脚。
【阅读提示】业务层不要直接依赖本文件；优先通过 HAL/SYS 接口使用硬件能力。
*/
#pragma once
#include <Arduino.h>

namespace BSP::Pins
{
    // 【输入】旋钮 A/B 相和两个实体按键。
    constexpr int KNOB_A = 5;
    constexpr int KNOB_B = 4;
    constexpr int BTN_MAIN = 6;
    constexpr int BTN_SIDE = 7;

    // 【音频/显示电源】I2S 输出到 MAX98357A/MAX98537 类功放，AUDIO_SD 控制功放关断，BACKLIGHT 控制屏幕背光。
    constexpr int I2S_BCLK = 18;
    constexpr int I2S_LRC = 13;
    constexpr int I2S_DOUT = 17;
    constexpr int AUDIO_SD = 48;
    constexpr int BACKLIGHT = 40;

    // 【电源检测】BAT_ADC 读取分压后的电池电压，CHRG 读取充电芯片状态。
    constexpr int BAT_ADC = 8;
    constexpr int CHRG = 16;

    // 【I2C 外设】当前主要用于 DRV2605L 震动驱动。
    constexpr int I2C_SDA = 41;
    constexpr int I2C_SCL = 42;

    // 【NFC】PN532 使用独立 SPI 引脚，RESET 用于休眠和故障恢复。
    constexpr int NFC_SCK = 1;
    constexpr int NFC_MISO = 2;
    constexpr int NFC_MOSI = 47;
    constexpr int NFC_SS = 15;
    constexpr int NFC_RESET = 21;
}
