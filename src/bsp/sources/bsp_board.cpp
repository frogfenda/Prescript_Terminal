/*
【模块职责】板级总入口实现。收束不依赖SYS/HAL的独立外设初始化；共享总线传感器仍由各SYS拥有。
*/
#include "bsp/bsp_board.h"
#include "bsp/bsp_flash_w25n01.h"

namespace BSP::Board
{
    // 【函数说明】板级最早期入口。当前保持空实现，用来保留未来统一初始化顺序的位置。
    void BeginEarly()
    {
        // 当前 WiFi 关闭、串口初始化等仍由 main.cpp 控制，避免改变开机时序。
    }

    // 【函数说明】板级常规入口。初始化独占SPI3的外挂NAND；失败不阻止内部文件系统和APP启动。
    void Begin()
    {
        (void)BSP::W25n01::Begin();
        BSP::W25n01::PrintDiagnostics();
    }
}

