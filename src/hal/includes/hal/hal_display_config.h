/*
【模块职责】当前 NV3007 逻辑画布和物理面板映射参数。
【边界】这些参数只描述显示 HAL 的硬件映射，不包含页面布局和业务资源路径。
*/
#pragma once

#include <Arduino.h>

namespace HAL::DisplayConfig
{
    constexpr uint16_t LOGICAL_WIDTH = 428;
    constexpr uint16_t LOGICAL_HEIGHT = 142;
    constexpr uint8_t ROTATION = 3;
    constexpr int16_t RAM_OFFSET_X = 0;
    constexpr int16_t RAM_OFFSET_Y = 26;
    constexpr int16_t PUSH_X = 0;
    constexpr int16_t PUSH_Y = 0;
}
