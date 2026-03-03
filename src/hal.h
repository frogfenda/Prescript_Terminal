#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>

#define PIN_KNOB_A D1
#define PIN_KNOB_B D2
#define PIN_BTN    0   
#define PIN_BUZZER D6

// 【新增】：多语言支持系统 (i18n)
typedef enum {
    LANG_EN = 0,
    LANG_ZH = 1
} SystemLang_t;

void Set_Language(SystemLang_t lang);
SystemLang_t Get_Language(void);

// === 暴露给应用层的外部接口 ===
void HAL_Init(void);
bool Is_Key_Pressed(void);
int  Get_Knob_Delta(void); 

void Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms);
void Buzzer_Random_Glitch(void);

void Screen_Clear(void);
void Screen_DrawHeader(void);
void Screen_DrawStandbyImage(void);
void Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str);
void Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str);
void Screen_DrawMenu(int current_selection); 
void Screen_Scroll_Up(uint8_t scroll_pixels);
void Screen_Update(void);

#endif