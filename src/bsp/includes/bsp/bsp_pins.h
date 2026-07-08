#pragma once
#include <Arduino.h>

#ifndef BSP_BOARD_PROFILE_V4B
#define BSP_BOARD_PROFILE_V4B 1
#endif

#ifndef BSP_BOARD_PROFILE
#define BSP_BOARD_PROFILE BSP_BOARD_PROFILE_V4B
#endif
#if BSP_BOARD_PROFILE == BSP_BOARD_PROFILE_V4B
#include "bsp/boards/bsp_pins_v4b.h"
#else
#error "Unsupported BSP_BOARD_PROFILE"
#endif
