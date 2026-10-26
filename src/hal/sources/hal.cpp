/*
【模块职责】HAL 初始化门面。

显示、输入和休眠分别由同层的专用实现文件拥有；本文件只负责按固定顺序完成一次初始化，
不读取资源、不调用 SYS 服务，也不暴露板级引脚。
*/
#include "hal/hal.h"
#include "hal_internal.h"

#include "bsp/bsp_power.h"

void HAL_Init()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    HAL_Display_Init();
    BSP::Power::BeginRails();
    HAL_Input_Init();
}
