#include "hal.h"

TFT_eSPI tft = TFT_eSPI();
// 创建一个局部的“精灵显存”，专门用来放会滚动的乱码文字
TFT_eSprite textSprite = TFT_eSprite(&tft);

// ================== 硬件初始化 ==================
void HAL_Init()
{
    // 1. 初始化屏幕
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    // 如果你的屏幕黑底发白/红底发蓝，取消下面这行的注释
    // tft.invertDisplay(true);

    // 2. 初始化局部显存 (宽240, 高160, 颜色深度8位省内存)
    textSprite.setColorDepth(8);
    textSprite.createSprite(240, 160);
    textSprite.fillSprite(TFT_BLACK);

    // 3. 初始化按键 (带内部上拉) 与 蜂鸣器
    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
}

// ================== 按键与声音 ==================
bool Is_Key_Pressed()
{
    // 按下为低电平 (0)
    return digitalRead(PIN_BTN) == LOW;
}

void Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms)
{
    // Arduino 框架自带的神级函数，硬件自动输出 PWM 声音！
    tone(PIN_BUZZER, freq, duration_ms);
    delay(duration_ms);
    noTone(PIN_BUZZER);
}

void Buzzer_Random_Glitch() {
    // 【核心修复】：加上 (unsigned int) 强转，消除编译器的类型歧义！
    tone(PIN_BUZZER, (unsigned int)(500 + random(2000)), 10);
}

// ================== 屏幕显存魔法 ==================
void Screen_Clear()
{
    tft.fillScreen(TFT_BLACK);
    textSprite.fillSprite(TFT_BLACK);
}

void Screen_DrawHeader()
{
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2); // 相当于 16x16 字体放大一倍
    tft.setCursor(10, 10);
    tft.print("[ PRESCRIPT ]");
}

void Screen_ShowTextLine(uint8_t x, uint8_t y, const char *str)
{
    // 画在局部显存上，设置暗金色
    textSprite.setTextColor(0xFDA0, TFT_BLACK);
    textSprite.setTextSize(2);
    textSprite.setCursor(x, y);
    textSprite.print(str);
}

void Screen_Scroll_Up(uint8_t scroll_pixels)
{
    // 降维打击！不再需要手写双层 for 循环搬运数组
    // TFT_eSPI 自带的显存滚动函数，一步搞定！
    textSprite.scroll(0, -scroll_pixels);
}

void Screen_Update()
{
    // 将局部显存一口气推送到屏幕的 (0, 40) 坐标处
    // 完美避开顶部的 Header 区域！
    textSprite.pushSprite(0, 40);
}