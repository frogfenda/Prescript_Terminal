#pragma once
#include <Arduino.h>

namespace BSP::Pins
{
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

    constexpr int I2S_BCLK = 5;
    constexpr int I2S_LRC = 4;
    constexpr int I2S_DOUT = 6;
    constexpr int AUDIO_SD = 7;
    constexpr int BACKLIGHT = LCD_BLK;

    constexpr int BAT_ADC = 8;
    constexpr int CHRG = 16;

    constexpr int I2C_SDA = 18;
    constexpr int I2C_SCL = 17;

    constexpr int NFC_SCK = 1;
    constexpr int NFC_MISO = 2;
    constexpr int NFC_MOSI = 42;
    constexpr int NFC_SS = 40;
    constexpr int NFC_RESET = 41;
}

