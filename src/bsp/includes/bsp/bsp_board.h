/*
【模块职责】板级总入口。收束独占总线、无需SYS协调的外设初始化，并为setup/休眠流程预留统一入口。
【阅读提示】当前只接管外挂SPI NAND；共享I2C传感器和已有HAL外设仍保持原初始化所有权。
*/
#pragma once
#include <Arduino.h>

namespace BSP::Board
{
    // 【函数说明】最早期板级入口。适合放置不依赖 SYS/HAL 的无状态硬件准备动作。
    void BeginEarly();

    // 【函数说明】常规板级入口。初始化外挂NAND并输出诊断；失败不阻止内部文件系统和APP启动。
    void Begin();
}
