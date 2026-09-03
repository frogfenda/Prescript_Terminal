/*
【模块职责】当前终端（“新新原理图主线”+“拓展版原理图”）唯一有效的板级引脚定义。
【验证状态】本文件只依据 2026-08-31 原理图建立，尚未经过实板电气、方向或时序验证。
【冲突约束】GPIO39/40/41/42 专用于 W25N01，GPIO1 专用于 CHAG；不得再引入旧 PN532 SPI/RESET 用途。
*/
#pragma once
#include <Arduino.h>

namespace BSP::Pins
{
    // NV3007 QSPI 显示引脚。
    constexpr int LCD_CS = 10;
    constexpr int LCD_SCL = 12;
    constexpr int LCD_SDA0 = 11;
    constexpr int LCD_SDA1 = 13;
    constexpr int LCD_SDA2 = 14;
    constexpr int LCD_SDA3 = 9;
    constexpr int LCD_BLK = 45;
    constexpr int LCD_RST = 3;
    constexpr int LCD_TE = 46;

    constexpr int KNOB_A = 48;
    constexpr int KNOB_B = 47;
    constexpr int BTN_MAIN = 21;
    constexpr int BTN_SIDE = 15;

    // NS4168 使用标准 I2S；GPIO7 的 CTRL 低电平关断，高电平启用并选择右声道。
    constexpr int I2S_BCLK = 5;
    constexpr int I2S_LRC = 4;
    constexpr int I2S_DOUT = 6;
    constexpr int AUDIO_AMP_CTRL = 7;
    constexpr int BACKLIGHT = LCD_BLK;

    constexpr int BAT_ADC = 8;
    constexpr int CHRG = 1; // BQ24075 CHG#/CHAG，低电平表示正在充电。
    constexpr int BAT_DIVIDER_TOP_OHMS = 1000000;
    constexpr int BAT_DIVIDER_BOTTOM_OHMS = 1000000;

    constexpr int I2C_SDA = 18;
    constexpr int I2C_SCL = 17;
    // 扩展板 PCF8563 INT# 为开漏低有效；BSP 负责电平，HAL 将其与 ESP 定时器兜底一起登记为 Light Sleep 唤醒源。
    constexpr int RTC_INT = 2;

    // W25N01 只在此登记并保留引脚；本次不实现 NAND BSP，也不把它冒充现有 FFat 分区。
    constexpr int NAND_CS = 42;
    constexpr int NAND_SCLK = 41;
    constexpr int NAND_MISO = 40;
    constexpr int NAND_MOSI = 39;

    // FM17550 使用 UART0：GPIO43 发往扩展板 UART_RX，GPIO44 接收扩展板 UART_TX。
    // 扩展板用 SET0/SET1 下拉固定 UART 模式，NPD 未引出，运行期只能软件复位/软休眠。
    constexpr int NFC_UART_TX = 43;
    constexpr int NFC_UART_RX = 44;
}
