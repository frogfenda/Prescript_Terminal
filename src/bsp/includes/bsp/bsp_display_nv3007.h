/*
【模块职责】NV3007/NV3006A1 长条屏板级初始化。HAL 负责绘图，BSP 只负责屏幕控制器初始化和休眠命令。
*/
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace BSP::DisplayNv3007
{
    // 【函数说明】发送 2.79 寸 142×428 长条屏的 NV3007/NV3006A1 初始化序列。
    void Init142x428(TFT_eSPI &tft);

    // 【函数说明】让屏幕控制器进入 sleep in 状态；背光开关由 BSP::Power 处理。
    void Sleep(TFT_eSPI &tft);

    // 【函数说明】让屏幕控制器退出 sleep 状态；画面刷新和背光点亮由 HAL 统一安排。
    void Wakeup(TFT_eSPI &tft);
}
