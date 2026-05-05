// 文件：src/hal/hal.h
#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../sys/sys_audio.h"
#include "../sys/sys_constants.h"

// Hardware/UI aliases kept for compatibility with the existing codebase.
// The source of truth is sys/sys_constants.h.
#define PIN_KNOB_A      PrescriptConst::PIN_KNOB_A
#define PIN_KNOB_B      PrescriptConst::PIN_KNOB_B
#define PIN_BTN         PrescriptConst::PIN_BTN_MAIN
#define PIN_BTN2        PrescriptConst::PIN_BTN_SIDE
#define PIN_I2S_BCLK    PrescriptConst::PIN_I2S_BCLK
#define PIN_I2S_LRC     PrescriptConst::PIN_I2S_LRC
#define PIN_I2S_DOUT    PrescriptConst::PIN_I2S_DOUT
#define PIN_AUDIO_SD    PrescriptConst::PIN_AUDIO_SD
#define PIN_BLK         PrescriptConst::PIN_BACKLIGHT
#define PIN_BAT_ADC     PrescriptConst::PIN_BAT_ADC
#define PIN_CHRG        PrescriptConst::PIN_CHRG

#define UI_HEADER_HEIGHT PrescriptConst::UI_HEADER_HEIGHT
#define UI_MARGIN_LEFT   PrescriptConst::UI_MARGIN_LEFT
#define UI_MARGIN_RIGHT  PrescriptConst::UI_MARGIN_RIGHT
#define UI_TEXT_Y_TOP    PrescriptConst::UI_TEXT_Y_TOP
#define UI_TIME_SAFE_PAD PrescriptConst::UI_TIME_SAFE_PAD


enum BtnEvent {
    BTN_NONE = 0,
    BTN_SHORT,   // 短按
    BTN_LONG,    // 长按
    BTN_DOUBLE   // 双击
};

void HAL_Btn2_Init();
// 暴露给外界的两个获取事件的接口
BtnEvent HAL_Get_Btn_Main_Event(); // 旋钮主按键
BtnEvent HAL_Get_Btn2_Event();     // 侧边副按键

void HAL_Init(void);
bool HAL_Is_Key_Pressed(void);
int  HAL_Get_Knob_Delta(void); 



void HAL_Screen_Clear(void);
void HAL_Screen_DrawHeader(void);
void HAL_Screen_DrawStandbyImage(void);

// 【核心修复】：全部打破 uint8_t 封印，改为支持负数和大坐标的 int32_t！
void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char* str);
void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char* str);
void HAL_Screen_ShowChineseLine_Faded(int32_t x, int32_t y, const char* str, float distance);
void HAL_Screen_ShowChineseLine_Faded_Color(int32_t x, int32_t y, const char* str, float distance, uint16_t base_color);

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels);
void HAL_Screen_Update(void);

int HAL_Get_Text_Width(const char* str);

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color);
void HAL_Sprite_Clear(void); 
void HAL_Sleep_Enter_Prepare();
void HAL_Sleep_Start();
void HAL_Sleep_Wakeup_Post();
void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h);
void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data);
uint16_t HAL_Get_Screen_Width(void);
uint16_t HAL_Get_Screen_Height(void);
void HAL_Screen_ShowChineseLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);
void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);
#endif