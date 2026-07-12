/*
【模块职责】DRV2605L 触觉驱动板级实现。所有 Wire1 与 0x5A 寄存器交互集中在这里。
*/
#include "bsp/bsp_haptic_drv2605.h"
#include "bsp/bsp_pins.h"
#include <Wire.h>

namespace
{
    // DRV2605L 固定 I2C 地址。放在 BSP 内部，避免板级驱动反向依赖 SYS 常量。
    constexpr uint8_t kDrv2605Address = 0x5A;

    // 记录 DRV2605L 最近一次初始化或寄存器写入是否正常。
    bool s_ready = false;
}

namespace BSP::HapticDrv2605
{
    // 【函数说明】向 DRV2605L 写入单个寄存器。
    bool WriteReg(uint8_t reg, uint8_t value)
    {
        // 所有 Wire1 访问集中在 BSP，sys_haptic 只负责选择业务波形。
        Wire1.beginTransmission(kDrv2605Address);
        Wire1.write(reg);
        Wire1.write(value);
        uint8_t err = Wire1.endTransmission();
        if (err != 0)
        {
            s_ready = false;
            Serial.printf("[BSP][震动] DRV2605L 寄存器写入失败：reg=0x%02X err=%u。\n", reg, err);
            return false;
        }
        return true;
    }

    // 【函数说明】初始化 DRV2605L 所在 I2C 总线和基础工作模式。
    bool Begin()
    {
        // 当前板子将 DRV2605L 接在 Wire1，和默认 Wire 分开，避免影响其他 I2C 设备扩展。
        Wire1.begin(Pins::I2C_SDA, Pins::I2C_SCL);
        Wire1.setTimeOut(20);

        // 退出待机、选择额定电压/过驱参数、切换到内部 ROM 波形库。
        bool ok = true;
        ok &= WriteReg(0x01, 0x00);
        ok &= WriteReg(0x1A, 0xB6);
        ok &= WriteReg(0x03, 0x06);
        s_ready = ok;
        Serial.println(ok ? "[BSP][震动] DRV2605L 初始化完成。" : "[BSP][震动] DRV2605L 初始化失败。");
        return ok;
    }

    // 【函数说明】返回 DRV2605L 当前是否可用。
    bool IsReady()
    {
        return s_ready;
    }

    // 【函数说明】写入波形序列并触发播放。
    bool PlaySequence(uint8_t w1, uint8_t w2, uint8_t w3, uint8_t wait1, uint8_t wait2)
    {
        if (!s_ready)
            return false;

        // DRV2605L 的等待片段需要最高位置 1；0 表示不插入等待。
        bool ok = true;
        ok &= WriteReg(0x04, w1);
        ok &= WriteReg(0x05, wait1 ? (0x80 | wait1) : 0x00);
        ok &= WriteReg(0x06, w2);
        ok &= WriteReg(0x07, wait2 ? (0x80 | wait2) : 0x00);
        ok &= WriteReg(0x08, w3);
        ok &= WriteReg(0x09, 0x00);
        ok &= WriteReg(0x0A, 0x00);
        ok &= WriteReg(0x0B, 0x00);

        // GO=1 后芯片按 0x04 起始的序列自动播放。
        ok &= WriteReg(0x0C, 0x01);
        return ok;
    }

    // 【函数说明】进入待机模式，降低休眠期间功耗。
    void Sleep()
    {
        WriteReg(0x01, 0x40);
    }

    // 【函数说明】退出待机模式，恢复波形播放能力。
    void Wakeup()
    {
        WriteReg(0x01, 0x00);
    }
}

