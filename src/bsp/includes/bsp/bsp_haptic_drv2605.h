/*
【模块职责】DRV2605L 触觉驱动板级接口。负责 I2C 初始化和寄存器写入，震动策略留在 sys_haptic。
*/
#pragma once
#include <Arduino.h>

namespace BSP::HapticDrv2605
{
    // 【函数说明】初始化 I2C 总线和 DRV2605L 基础寄存器。
    bool Begin();

    // 【函数说明】返回 DRV2605L 最近一次初始化/写入是否处于可用状态。
    bool IsReady();

    // 【函数说明】向 DRV2605L 写入一个寄存器，是本模块所有震动动作的底层原语。
    bool WriteReg(uint8_t reg, uint8_t value);

    // 【函数说明】写入最多三段 ROM 波形和等待片段，然后触发 GO 寄存器播放。
    bool PlaySequence(uint8_t w1, uint8_t w2 = 0, uint8_t w3 = 0, uint8_t wait1 = 0, uint8_t wait2 = 0);

    // 【函数说明】让 DRV2605L 进入待机，减少休眠期间功耗。
    void Sleep();

    // 【函数说明】让 DRV2605L 退出待机，恢复寄存器访问和震动播放能力。
    void Wakeup();
}
