/* HAL 输入设备的去抖和手势时间参数；不包含页面或业务配置。 */
#pragma once

#include <stdint.h>

namespace HAL::InputConfig
{
    constexpr uint32_t BUTTON_LONG_MS = 800UL;
    constexpr uint32_t BUTTON_DOUBLE_GAP_MS = 100UL;
    constexpr uint32_t BUTTON_MAIN_DEBOUNCE_MS = 55UL;
    constexpr uint32_t BUTTON_SIDE_DEBOUNCE_MS = 35UL;
}
