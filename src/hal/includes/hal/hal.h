// 文件：src/hal/hal.h
#ifndef __HAL_H
#define __HAL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "sys/sys_audio.h"

/*
 * HAL 对外硬件接口。
 *
 * 说明：
 * - 上层只通过 HAL 函数读取输入、绘制屏幕和进入休眠；
 * - 板级引脚不再从 HAL 公共头暴露，避免 APP/SYS 继续依赖旧 PIN_XXX 宏。
 */

// 旧 UI 常量别名，兼容已有代码。
#include "sys/sys_constants.h"
#define UI_HEADER_HEIGHT PrescriptConst::UI_HEADER_HEIGHT
#define UI_MARGIN_LEFT   PrescriptConst::UI_MARGIN_LEFT
#define UI_MARGIN_RIGHT  PrescriptConst::UI_MARGIN_RIGHT
#define UI_TEXT_Y_TOP    PrescriptConst::UI_TEXT_Y_TOP
#define UI_TIME_SAFE_PAD PrescriptConst::UI_TIME_SAFE_PAD

enum BtnEvent {
    BTN_NONE = 0,
    BTN_SHORT,
    BTN_LONG,
    BTN_DOUBLE
};

/**
 * HAL 字体角色。
 *
 * 说明：
 * - 小字用于 HUD、状态提示和低优先级文字；
 * - 正文字体用于菜单、滚轮和普通页面文本；
 * - 标题字体预留给后续大标题/章节标题，目前可和正文使用同一字体。
 *
 * 字体具体选择和 baseline/lineHeight 在 src/ui/ui_font_config.h 中配置。
 */
enum HALFontRole {
    HAL_FONT_SMALL = 0,
    HAL_FONT_BODY,
    HAL_FONT_TITLE
};

void HAL_Btn2_Init();
BtnEvent HAL_Get_Btn_Main_Event();
BtnEvent HAL_Get_Btn2_Event();

void HAL_Init(void);
bool HAL_Is_Key_Pressed(void);
int  HAL_Get_Knob_Delta(void);

void HAL_Screen_Clear(void);
void HAL_Screen_DrawHeader(void);
void HAL_Screen_DrawStandbyImage(void);

void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char* str);
void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char* str);
void HAL_Screen_ShowChineseLine_Faded(int32_t x, int32_t y, const char* str, float distance);
void HAL_Screen_ShowChineseLine_Faded_Color(int32_t x, int32_t y, const char* str, float distance, uint16_t base_color);
void HAL_Screen_ShowChineseLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);
void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);

/** 按指定字体角色绘制一行 UTF-8 文本。y 表示文本框顶部，HAL 会按角色 baseline 放置字形。 */
void HAL_Screen_ShowLine_Font(int32_t x, int32_t y, const char* str, HALFontRole role, uint16_t color = TFT_CYAN);

/** 绘制小字体文本，供 HUD 和状态提示使用。 */
void HAL_Screen_ShowSmallLine(int32_t x, int32_t y, const char* str);
void HAL_Screen_ShowSmallLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);

/** 查询字体度量。用于 UI 层按照当前字体计算框线、行高和居中位置。 */
int HAL_Get_Font_Baseline(HALFontRole role);
int HAL_Get_Font_Line_Height(HALFontRole role);
int HAL_Get_Text_Width_Font(const char* str, HALFontRole role);
int HAL_Get_Text_Width_Small(const char* str);

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels);
void HAL_Screen_Update(void);
void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h);

int HAL_Get_Text_Width(const char* str);

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color);
void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data);
void HAL_Sprite_Clear(void);

void HAL_Sleep_Enter_Prepare();
void HAL_Sleep_Start();
void HAL_Sleep_Wakeup_Post();

uint16_t HAL_Get_Screen_Width(void);
uint16_t HAL_Get_Screen_Height(void);

#endif
