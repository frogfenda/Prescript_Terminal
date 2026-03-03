#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// === 引脚定义 ===
// 假设你已经把屏幕的 RST 接到了板子的 RST 上，释放了 D2
#define PIN_KNOB_A D1  // 旋钮 A 脚
#define PIN_KNOB_B D2  // 旋钮 B 脚
#define PIN_BTN    0   // 板载 FLASH 按钮 (GPIO0) / 或者旋钮按键接 D3(GPIO0)
#define PIN_BUZZER D6  // 蜂鸣器引脚

// === 暴露给应用层的外部接口 ===
void HAL_Init(void);
bool Is_Key_Pressed(void);
int  Get_Knob_Delta(void); // 【新增】：获取旋钮的转动步数 (正数右转，负数左转)

void Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms);
void Buzzer_Random_Glitch(void);

// === 屏幕专用接口 ===
void Screen_Clear(void);
void Screen_DrawHeader(void);
void Screen_DrawStandbyImage(void);
void Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str);
void Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str); // 【新增】：中文输出接口
void Screen_Scroll_Up(uint8_t scroll_pixels);
void Screen_Update(void);

#endif