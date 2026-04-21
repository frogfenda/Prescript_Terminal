#include "sys_haptic.h"
#include "sys_config.h"
#include <Wire.h>

#define DRV2605_ADDR 0x5A
#define PIN_I2C_SDA 41
#define PIN_I2C_SCL 42

SysHaptic sysHaptic;

void drv_write(uint8_t reg, uint8_t val)
{
    Wire1.beginTransmission(DRV2605_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

void SysHaptic::begin()
{
    Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire1.setTimeOut(20);

    drv_write(0x01, 0x00);
    drv_write(0x1A, 0xB6);
    drv_write(0x03, 0x06); // 使用内部 LRA 预设库 (通常为 6)
}

void playWaveform(uint8_t base_effect)
{
    if (!sysConfig.haptic_enable)
        return;

    uint8_t final_effect = base_effect;

    // ==============================================================
    // 动态震感强度映射 (根据 sysConfig.haptic_intensity 切换波形编号)
    // ==============================================================
    if (base_effect == 4)
    {
        // 确认键 (Sharp Click 系列)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 5; // 60% 强度
        if (sysConfig.haptic_intensity == 1)
            final_effect = 6; // 30% 强度
    }
    else if (base_effect == 7)
    {
        // 旋钮滚动 (Soft Bump 微震系列，极短)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 8; // 60% 强度微震
        if (sysConfig.haptic_intensity == 1)
            final_effect = 9; // 30% 强度微震
    }
    else if (base_effect == 10)
    {
        // 返回键 (Double Click 双击系列)
        if (sysConfig.haptic_intensity == 2)
            final_effect = 11; // 60% 强度双击
        if (sysConfig.haptic_intensity == 1)
            final_effect = 12; // 30% 强度双击
    }

    drv_write(0x04, final_effect); // 写入第一个波形
    drv_write(0x05, 0x00);         // 0x00 代表序列结束
    drv_write(0x0C, 0x01);         // 触发播放 GO!
}

// ==============================================================
// 核心波形编号重构 (告别拖泥带水，全面改用极短波形)
// ==============================================================
void SysHaptic::playTick() { playWaveform(7); }      // 7: Soft Bump 100% (极短微震，适合旋钮连续滚)
void SysHaptic::playConfirm() { playWaveform(4); }   // 4: Sharp Click 100% (极短清脆，适合按压)
void SysHaptic::playBack() { playWaveform(10); }     // 10: Double Click (快速双击，作为退出/返回的触觉区分)
void SysHaptic::playCoinHeads() { playWaveform(1); } // 1: Strong Click 100% (硬币抛出重击)
void SysHaptic::playCoinTails() { playWaveform(2); } // 2: Strong Click 60% (反面稍弱一点)
void SysHaptic::playAlert() { playWaveform(15); }    // 15: 警告类

// 复合连击波形：解码成功 (原版的全是 10 太拖拉，换成渐强连击)
void SysHaptic::playDecodeSuccess()
{
    if (!sysConfig.haptic_enable)
        return;
    drv_write(0x04, 6);         // Sharp Click 30%
    drv_write(0x05, 0x80 | 10); // 等待 10ms
    drv_write(0x06, 5);         // Sharp Click 60%
    drv_write(0x07, 0x80 | 10); // 等待 10ms
    drv_write(0x08, 4);         // Sharp Click 100% (渐强)
    drv_write(0x09, 0x00);      // 结束符
    drv_write(0x0A, 0x00);
    drv_write(0x0B, 0x00);
    drv_write(0x0C, 0x01); // 触发播放
}

void SysHaptic_Sleep()
{
    Wire1.beginTransmission(0x5A);
    Wire1.write(0x01);
    Wire1.write(0x40);
    Wire1.endTransmission();
}

void SysHaptic_Wakeup()
{
    Wire1.beginTransmission(0x5A);
    Wire1.write(0x01);
    Wire1.write(0x00);
    Wire1.endTransmission();
}