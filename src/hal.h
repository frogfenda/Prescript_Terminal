#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// === 引脚定义 (ESP8266 NodeMCU 丝印) ===
#define PIN_BTN    D1  // 按钮引脚
#define PIN_BUZZER D6  // 蜂鸣器引脚

// === 暴露给应用层的外部接口 ===
void HAL_Init(void);
bool Is_Key_Pressed(void);
void Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms);
void Buzzer_Random_Glitch(void);

// === 屏幕专用接口 ===
void Screen_Clear(void);
void Screen_DrawHeader(void);
void Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str);
void Screen_Scroll_Up(uint8_t scroll_pixels);
void Screen_Update(void);

#endif