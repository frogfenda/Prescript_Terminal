/*
【模块职责】NV3007/NV3006A1 QSPI 长条屏板级驱动。直接管理 QSPI 初始化、TE 同步、背光、休眠和 RGB565 图像刷新。
*/
#pragma once
#include <Arduino.h>
#include "esp_err.h"

namespace BSP::DisplayNv3007
{
    constexpr uint16_t PANEL_WIDTH = 168;
    constexpr uint16_t PANEL_HEIGHT = 428;
    constexpr uint16_t PANEL_X_GAP = 12;
    constexpr uint16_t PANEL_Y_GAP = 0;

    struct Diagnostics
    {
        bool ready;
        esp_err_t lastError;
        bool teReady;
        uint32_t tePhaseUs;
        uint32_t tePeriodUs;
        uint32_t teWaitNextUs;
        uint32_t teIrqAgeUs;
        uint32_t teIrqCount;
        uint32_t teWaitCount;
        uint32_t teTimeoutCount;
    };

    // 【函数说明】初始化 QSPI 总线、NV3007 控制器、TE 中断和背光 GPIO。
    bool Begin();

    // 【函数说明】返回屏幕控制器和 SPI 通道当前是否可刷新。
    bool IsReady();

    // 【函数说明】返回最近一次底层 SPI/初始化错误码。
    esp_err_t LastError();

    // 【函数说明】直接控制背光；当前硬件为高电平点亮。
    void SetBacklight(bool on);

    // 【函数说明】整屏填充 RGB565 颜色。
    bool FillScreen(uint16_t color);

    // 【函数说明】在物理竖屏坐标系中填充矩形。
    bool FillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color);

    /*
     * 【函数说明】在物理竖屏坐标系中推送 RGB565 图像。
     * sourceByteSwapped=true 用于 TFT_eSprite 这类内部以交换字节保存 16-bit 像素的源。
     */
    bool PushImage(int16_t x,
                   int16_t y,
                   uint16_t w,
                   uint16_t h,
                   const uint16_t *pixels,
                   uint16_t srcStride = 0,
                   bool sourceByteSwapped = false);

    /*
     * 【函数说明】按旋转后的逻辑坐标推送 RGB565 图像。
     * rotation 遵循 TFT_eSPI 风格：0=竖屏，1=顺时针横屏，2=倒竖屏，3=逆时针横屏。
     */
    bool PushImageRotated(uint8_t rotation,
                          int16_t x,
                          int16_t y,
                          uint16_t w,
                          uint16_t h,
                          const uint16_t *pixels,
                          uint16_t srcStride = 0,
                          bool sourceByteSwapped = false);

    // 【函数说明】等待 TE 参考相位。若 TE 不可用则直接放行，避免阻塞上层 UI。
    bool WaitTearEffectPhase(uint32_t timeoutMs = 25);

    // 【函数说明】设置相对 TE 上升沿的刷新起始相位，单位微秒。
    void SetTearEffectPhaseUs(uint32_t phaseUs);

    // 【函数说明】读取当前显示诊断信息。
    Diagnostics GetDiagnostics();

    // 【函数说明】打印当前显示诊断信息，便于串口排查屏幕时序。
    void PrintDiagnostics(Stream &out = Serial);

    // 【函数说明】让屏幕控制器进入 sleep in 状态；背光开关由 BSP::Power 或 SetBacklight 处理。
    void Sleep();

    // 【函数说明】让屏幕控制器退出 sleep 状态；画面刷新和背光点亮由 HAL 统一安排。
    void Wakeup();

    // Send display-on after GRAM has been refreshed, so the backlight never reveals a blank wake frame.
    void DisplayOn();
}
