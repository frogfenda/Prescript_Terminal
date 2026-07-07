/*
【模块职责】板级引脚定义。集中描述当前 ESP32-S3 主板连接到屏幕、I2C 外设、音频、NFC、电源检测和输入件的物理引脚。
【阅读提示】业务层不要直接依赖本文件；优先通过 HAL/SYS 接口使用硬件能力。
*/
#pragma once
#include <Arduino.h>

namespace BSP::Pins
{
    // 【显示】NV3007 QSPI 4-wire 接口。BLK 为高电平点亮，TE 用于避开撕裂刷新相位。
    constexpr int LCD_CS = 10;
    constexpr int LCD_SCL = 12;
    constexpr int LCD_SDA0 = 11;
    constexpr int LCD_SDA1 = 13;
    constexpr int LCD_SDA2 = 14;
    constexpr int LCD_SDA3 = 9;
    constexpr int LCD_BLK = 45;
    constexpr int LCD_RST = 3;
    constexpr int LCD_TE = 46;

    // 【输入】旋钮 A/B 相、旋钮按下和独立按键。
    constexpr int KNOB_A = 48;
    constexpr int KNOB_B = 47;
    constexpr int BTN_MAIN = 21;
    constexpr int BTN_SIDE = 15;

    // 【音频/显示电源】I2S 输出到 MAX98357A/MAX98537 类功放，AUDIO_SD 控制功放关断，BACKLIGHT 控制屏幕背光。
    constexpr int I2S_BCLK = 5;
    constexpr int I2S_LRC = 4;
    constexpr int I2S_DOUT = 6;
    constexpr int AUDIO_SD = 7;
    constexpr int BACKLIGHT = LCD_BLK;

    // 【电源检测】BAT_ADC 读取分压后的电池电压，CHRG 读取充电芯片状态。
    constexpr int BAT_ADC = 8;
    constexpr int CHRG = 16;

    // 【I2C 外设】PCF8563 / TM6605 / LSM6DSL / QMC5883P 共用总线。
    constexpr int I2C_SDA = 18;
    constexpr int I2C_SCL = 17;

    // 【NFC】PN532 使用独立 SPI 引脚，RESET 用于休眠和故障恢复。
    constexpr int NFC_SCK = 1;
    constexpr int NFC_MISO = 2;
    constexpr int NFC_MOSI = 42;
    constexpr int NFC_SS = 40;
    constexpr int NFC_RESET = 41;
}
