/*
【模块职责】HAL 对外接口。上层只通过这里读取旋钮、主按键、副按键，并向 284×76 逻辑画布绘制文本/线框/图片；真实引脚、ST7789 Sprite 推屏、休眠唤醒细节都封装在 hal.cpp。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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

// 【接口说明】初始化副按键 GPIO 和内部按键状态机；主按键在 HAL_Init 中统一初始化，副按键单独暴露给早期调试和兼容代码。
void HAL_Btn2_Init();
// 暴露给外界的两个获取事件的接口
// 【接口说明】读取旋钮主按键的短按、长按、双击事件；函数返回一次后事件会被消费，AppManager 每帧调用它分发给当前 App。
BtnEvent HAL_Get_Btn_Main_Event(); // 旋钮主按键
BtnEvent HAL_Get_Btn2_Event();     // 侧边副按键

// 【接口说明】初始化屏幕 Sprite、U8g2 中文字体、旋钮中断、主副按键、背光、功放使能和 ADC 输入，是 setup 中进入 App 前的硬件准备。
void HAL_Init(void);
bool HAL_Is_Key_Pressed(void);
// 【接口说明】返回并清零旋钮中断累计的步进值；内部关中断读取计数，避免 ISR 和主循环同时访问 raw_knob_counter。
int  HAL_Get_Knob_Delta(void); 



void HAL_Screen_Clear(void);
// 【接口说明】绘制基础终端边框/页眉线，给非菜单类页面作为统一窗口底板。
void HAL_Screen_DrawHeader(void);
void HAL_Screen_DrawStandbyImage(void);

// 【核心修复】：全部打破 uint8_t 封印，改为支持负数和大坐标的 int32_t！
// 【接口说明】使用 TFT_eSPI 默认字体在逻辑画布上绘制英文/数字文本，适合时间和协议状态短标签。
void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char* str);
void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char* str);
// 【接口说明】按 distance 计算灰度衰减后绘制文字，菜单滚轮和弧形刻度盘用它表现远离中心的淡出效果。
void HAL_Screen_ShowChineseLine_Faded(int32_t x, int32_t y, const char* str, float distance);
void HAL_Screen_ShowChineseLine_Faded_Color(int32_t x, int32_t y, const char* str, float distance, uint16_t base_color);

// 【接口说明】直接滚动 Sprite 内容，用于需要局部文字上移的界面。
void HAL_Screen_Scroll_Up(uint8_t scroll_pixels);
void HAL_Screen_Update(void);

// 【接口说明】用当前 U8g2 字体测量 UTF-8 字符串宽度，菜单居中、右上角时间对齐和文本裁剪都依赖它。
int HAL_Get_Text_Width(const char* str);

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
// 【接口说明】在逻辑画布画矩形边框，弹窗、菜单选中框、电池图标都通过它完成。
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
// 【接口说明】在逻辑画布填充三角形，菜单选中指针使用它画朝向中心的箭头。
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color);
// 【接口说明】只清空逻辑 Sprite 不立即推屏，页面可以连续画多层 UI 后再调用 HAL_Screen_Update。
void HAL_Sprite_Clear(void); 
void HAL_Sleep_Enter_Prepare();
// 【接口说明】启动 ESP32-S3 Light Sleep；唤醒源由主按键配置，函数返回时设备已经被按键唤醒。
void HAL_Sleep_Start();
void HAL_Sleep_Wakeup_Post();
// 【接口说明】保留给局部刷新使用的接口；当前逻辑画布较小，主要路径仍使用 HAL_Screen_Update 全量推送。
void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h);
void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data);
// 【接口说明】返回终端 UI 的逻辑宽度 284，不直接暴露 ST7789 的物理宽度。
uint16_t HAL_Get_Screen_Width(void);
uint16_t HAL_Get_Screen_Height(void);
// 【接口说明】使用 U8g2 字体按指定 RGB565 颜色绘制文字，特殊指令完成态用于显示主题色文本。
void HAL_Screen_ShowChineseLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);
void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char* str, uint16_t color);
#endif
