/* HAL 内部实现协作接口；不加入公共 includes，避免上层依赖实现细节。 */
#pragma once

void HAL_Display_Init();
void HAL_Input_Init();
void HAL_Input_ResetAfterWake();
