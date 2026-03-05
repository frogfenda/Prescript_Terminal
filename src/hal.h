#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// 【更新】：ESP32-S3 外设的专属安全引脚
#define PIN_KNOB_A 4
#define PIN_KNOB_B 5
#define PIN_BTN    6   
#define PIN_BUZZER 7
// === 暴露给应用层的外部接口 ===
void HAL_Init(void);
bool HAL_Is_Key_Pressed(void);
int  HAL_Get_Knob_Delta(void); 

void HAL_Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms);
void HAL_Buzzer_Random_Glitch(void);

// === 屏幕基础操作 API ===
void HAL_Screen_Clear(void);
void HAL_Screen_DrawHeader(void);
void HAL_Screen_DrawStandbyImage(void);
void HAL_Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str);
void HAL_Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str);
void HAL_Screen_Scroll_Up(uint8_t scroll_pixels);
void HAL_Screen_Update(void);

// === 暴露给 UI 层的图元绘制 API ===
void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void HAL_Sprite_Clear(void); 

#endif