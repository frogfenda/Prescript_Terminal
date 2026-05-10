/*
【模块职责】DRV2605L 震动实现。用 Wire1 访问 0x5A，按全局震动开关和强度等级把基础波形映射到不同衰减 ROM 波形，并支持睡眠/唤醒。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys_haptic.h"
#include "sys_config.h"
#include "sys_constants.h"
#include <Wire.h>


SysHaptic sysHaptic;

// 【函数说明】向 DRV2605L 写一个寄存器值，所有震动波形配置都通过 Wire1 调用这个最小封装。
void drv_write(uint8_t reg, uint8_t val)
{
    Wire1.beginTransmission(PrescriptConst::DRV2605_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

// 【函数说明】初始化 Wire1 的 SDA/SCL，引导 DRV2605L 退出待机并选择内部 LRA 波形库。
void SysHaptic::begin()
{
    Wire1.begin(PrescriptConst::PIN_I2C_SDA, PrescriptConst::PIN_I2C_SCL);
    Wire1.setTimeOut(20);

    drv_write(0x01, 0x00);
    drv_write(0x1A, 0xB6);
    drv_write(0x03, 0x06); // 内部 LRA 预设库
}

// 【函数说明】根据全局震动开关和强度，把基础波形号映射到 100/60/30% 物理 ROM 波形，并触发 GO 寄存器。
void playWaveform(uint8_t base_effect)
{
    if (!sysConfig.haptic_enable)
        return;

    uint8_t final_effect = base_effect;

    // ==============================================================
    // 严格的三级强度映射 (对应芯片物理 ROM 内置的百分比衰减波形)
    // 强度 3 = 100%, 强度 2 = 60%, 强度 1 = 30%
    // ==============================================================

    if (base_effect == 4)
    {
        // Sharp Click (清脆单击：适合按键确认)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 5;
        if (sysConfig.haptic_intensity == 1)
            final_effect = 6;
    }
    else if (base_effect == 7)
    {
        // Soft Bump (极短微震：适合旋钮滚动，绝不拖泥带水)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 8;
        if (sysConfig.haptic_intensity == 1)
            final_effect = 9;
    }
    else if (base_effect == 10)
    {
        // Double Click (快速双击：适合退出/返回)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 11;
        if (sysConfig.haptic_intensity == 1)
            final_effect = 12;
    }
    else if (base_effect == 15)
    {
        // Alert (警报/长震)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 16;
        if (sysConfig.haptic_intensity == 1)
            final_effect = 17;
    }

    drv_write(0x04, final_effect);
    drv_write(0x05, 0x00);
    drv_write(0x0C, 0x01);
}

// 核心波形全面升级为清脆短波
// 【函数说明】播放旋钮移动使用的 Soft Bump 短震。
void SysHaptic::playTick() { playWaveform(7); }      // 旋钮：极短微震
void SysHaptic::playConfirm() { playWaveform(4); }   // 确认：清脆单击
// 【函数说明】播放返回/长按使用的 Double Click 震动。
void SysHaptic::playBack() { playWaveform(10); }     // 返回：双击段落
void SysHaptic::playCoinHeads() { playWaveform(4); } // 抛硬币正面：强确认
// 【函数说明】播放硬币反面结果的较弱震动。
void SysHaptic::playCoinTails() { playWaveform(5); } // 抛硬币反面：比正面弱一档
void SysHaptic::playAlert() { playWaveform(15); }    // 警告提示

// 复合连击也严格遵循用户的强度设置！
// 【函数说明】写入三段波形队列和短等待，形成解码完成的复合连击震动。
void SysHaptic::playDecodeSuccess()
{
    if (!sysConfig.haptic_enable)
        return;

    // 根据全局强度，动态选择连击的波形基础
    uint8_t w1 = 6; // 默认最弱
    uint8_t w2 = 5; // 默认中等
    uint8_t w3 = 4; // 默认最强

    if (sysConfig.haptic_intensity == 1)
    {
        w1 = 6;
        w2 = 6;
        w3 = 6;
    } // 用户设为弱，全用最弱波形
    else if (sysConfig.haptic_intensity == 2)
    {
        w1 = 6;
        w2 = 5;
        w3 = 5;
    } // 用户设为中

    drv_write(0x04, w1);
    drv_write(0x05, 0x80 | 10);
    drv_write(0x06, w2);
    drv_write(0x07, 0x80 | 10);
    drv_write(0x08, w3);
    drv_write(0x09, 0x00);
    drv_write(0x0A, 0x00);
    drv_write(0x0B, 0x00);
    drv_write(0x0C, 0x01);
}

// 【函数说明】把 DRV2605L MODE 写成 standby，休眠时停止触觉驱动。
void SysHaptic_Sleep()
{
    Wire1.beginTransmission(0x5A);
    Wire1.write(0x01);
    Wire1.write(0x40);
    Wire1.endTransmission();
}

// 【函数说明】把 DRV2605L MODE 写回 internal trigger，唤醒后恢复波形播放能力。
void SysHaptic_Wakeup()
{
    Wire1.beginTransmission(0x5A);
    Wire1.write(0x01);
    Wire1.write(0x00);
    Wire1.endTransmission();
}
