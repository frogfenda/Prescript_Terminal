// 文件：src/sys/sys_haptic.cpp
#include "sys_haptic.h"
#include "sys_config.h"
#include <Wire.h>
#include <mutex>

#define DRV2605_ADDR 0x5A
#define PIN_I2C_SDA  41
#define PIN_I2C_SCL  42

SysHaptic sysHaptic;

// 【极其重要】：I2C 全局防撞锁，为以后的 NFC 模块提前打好地基！
std::mutex g_i2c_mutex; 

// 寄存器写入底层函数 (带锁防护)
void drv_write(uint8_t reg, uint8_t val) {
    std::lock_guard<std::mutex> lock(g_i2c_mutex);
    Wire.beginTransmission(DRV2605_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void SysHaptic::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    // ==========================================
    // 唤醒 DRV2605L 并配置为 LRA 线性马达专享模式！
    // ==========================================
    drv_write(0x01, 0x00); // 退出待机，使用内部触发模式
    drv_write(0x1A, 0xB6); // 反馈控制：强行开启 LRA 模式 (Bit 7 = 1)
    drv_write(0x03, 0x06); // 强行挂载 Library 6 (LRA 专用极其干脆的波形库)
}

// 核心波形发射器 (自带智能强度映射降级)
void playWaveform(uint8_t base_effect) {
    if (!sysConfig.haptic_enable) return; // 用户关了震动，直接静默

    uint8_t final_effect = base_effect;
    
    // 赛博强度智能降级：不改电压，只换波形！保证手感始终干脆！
    if (base_effect == 1) { // 针对重击
        if (sysConfig.haptic_intensity == 2) final_effect = 2; // 中等重击
        if (sysConfig.haptic_intensity == 1) final_effect = 3; // 轻微重击
    } else if (base_effect == 10) { // 针对滴答
        if (sysConfig.haptic_intensity == 1) final_effect = 12; // 极弱滴答
    }
    
    drv_write(0x04, final_effect); // 将波形装填入槽位 1
    drv_write(0x05, 0x00);         // 槽位 2 填入休止符 (结束)
    drv_write(0x0C, 0x01);         // GO! 扣动扳机
}

// 动作库映射 (精心挑选的 DRV2605L 极品波形 ID)
void SysHaptic::playTick()      { playWaveform(10); } // ID 10: Tick 100%
void SysHaptic::playConfirm()   { playWaveform(1);  } // ID 1: Strong Click 100%
void SysHaptic::playBack()      { playWaveform(15); } // ID 15: Double Click
void SysHaptic::playCoinHeads() { playWaveform(24); } // ID 24: Transition Click (正面清脆)
void SysHaptic::playCoinTails() { playWaveform(27); } // ID 27: Transition Hum (反面沉闷)
void SysHaptic::playAlert()     { playWaveform(64); } // ID 64: Transient Alert (警告刺痛)

// 专属的高级序列组合：完美贴合你“滴滴滴滴”的四连音效
void SysHaptic::playDecodeSuccess() {
    if (!sysConfig.haptic_enable) return;

    // DRV2605L 支持连发序列！0x80 代表延迟，后面的数字是时间 (x10 毫秒)
    drv_write(0x04, 10);          // 第 1 滴
    drv_write(0x05, 0x80 | 10);   // 停顿 100ms
    drv_write(0x06, 10);          // 第 2 滴
    drv_write(0x07, 0x80 | 10);   // 停顿 100ms
    drv_write(0x08, 10);          // 第 3 滴
    drv_write(0x09, 0x80 | 10);   // 停顿 100ms
    drv_write(0x0A, 1);           // 第 4 下来个重击结尾！(ID 1)
    drv_write(0x0B, 0x00);        // 结束序列
    drv_write(0x0C, 0x01);        // GO!
}