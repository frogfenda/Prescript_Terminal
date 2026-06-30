/*
【模块职责】板级总入口。为 setup/休眠流程预留统一入口，后续新增外设时优先登记到这里。
【阅读提示】当前先保持轻量，不强行接管所有 SYS 初始化，避免改变既有启动顺序。
*/
#pragma once
#include <Arduino.h>

namespace BSP::Board
{
    // 【函数说明】最早期板级入口。适合放置不依赖 SYS/HAL 的无状态硬件准备动作。
    void BeginEarly();

    // 【函数说明】常规板级入口。后续新增外设时，可在保持启动顺序清晰的前提下逐步收束初始化。
    void Begin();
}
