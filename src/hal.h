// 文件：src/hal.h
#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>

#define PIN_KNOB_A 4
#define PIN_KNOB_B 5
#define PIN_BTN    6   
#define PIN_BUZZER 7

void HAL_Init(void);
bool HAL_Is_Key_Pressed(void);
int  HAL_Get_Knob_Delta(void); 

void HAL_Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms);
void HAL_Buzzer_Random_Glitch(void);

void HAL_Screen_Clear(void);
void HAL_Screen_DrawHeader(void);
void HAL_Screen_DrawStandbyImage(void);
void HAL_Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str);
void HAL_Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str);
void HAL_Screen_Scroll_Up(uint8_t scroll_pixels);
void HAL_Screen_Update(void);

// 【新增 API】：计算中文字符串在屏幕上的绝对像素宽度
int HAL_Get_Text_Width(const char* str);
// 【新增 API】：支持透明度渐变的文字渲染（实现 3D 景深淡出）
void HAL_Screen_ShowChineseLine_Faded(uint8_t x, uint8_t y, const char* str, float distance);

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void HAL_Sprite_Clear(void); 

#endif