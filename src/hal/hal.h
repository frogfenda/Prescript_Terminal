// 文件：src/hal/hal.h
#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../sys/sys_audio.h"

#define PIN_KNOB_A 5
#define PIN_KNOB_B 4
#define PIN_BTN    6   


#define PIN_I2S_BCLK 18
#define PIN_I2S_LRC  13
#define PIN_I2S_DOUT 17
#define PIN_AUDIO_SD 48 
#define PIN_BLK      40
// ==========================================
// 系统级 UI 布局尺寸定义
// ==========================================
#define UI_HEADER_HEIGHT   38  
#define UI_MARGIN_LEFT     20  
#define UI_MARGIN_RIGHT    20  
#define UI_TEXT_Y_TOP      16  
#define UI_TIME_SAFE_PAD   28  



// ==========================================
// 【新增】：通用按键事件枚举
// ==========================================
#define PIN_BTN2 7

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
#endif