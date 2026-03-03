#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>

#define PIN_KNOB_A D1
#define PIN_KNOB_B D2
#define PIN_BTN    0   
#define PIN_BUZZER D0    // 蜂鸣器搬家到 D0
#define PIN_ROM_CS 3    // 【新增】：字库芯片的 CS 片选脚，接在 D4

// === 国际化 (i18n) ===
typedef enum {
    LANG_EN = 0,
    LANG_ZH = 1
} SystemLang_t;

void Set_Language(SystemLang_t lang);
SystemLang_t Get_Language(void);
void Toggle_Language(void); // 【新增】：一键无缝切换语言

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