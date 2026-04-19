#include "sys_haptic.h"
#include "sys_config.h"
#include <Wire.h>
// 【删除】：去掉了讨厌的 mutex 锁！

#define DRV2605_ADDR 0x5A
#define PIN_I2C_SDA  41 // 马达保持原引脚不变
#define PIN_I2C_SCL  42

SysHaptic sysHaptic;

void drv_write(uint8_t reg, uint8_t val) {
    // 【修改】：全部换成 Wire1，走独立高速公路！
    Wire1.beginTransmission(DRV2605_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

void SysHaptic::begin() {
    // 【修改】：启动第二条硬件 I2C 总线
    Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire1.setTimeOut(20); // 加上 20ms 防弹护盾
    
    drv_write(0x01, 0x00); 
    drv_write(0x1A, 0xB6); 
    drv_write(0x03, 0x06); 
}

void playWaveform(uint8_t base_effect) {
    if (!sysConfig.haptic_enable) return;

    uint8_t final_effect = base_effect;
    if (base_effect == 1) { 
        if (sysConfig.haptic_intensity == 2) final_effect = 2; 
        if (sysConfig.haptic_intensity == 1) final_effect = 3; 
    } else if (base_effect == 10) { 
        if (sysConfig.haptic_intensity == 1) final_effect = 12; 
    }
    
    drv_write(0x04, final_effect); 
    drv_write(0x05, 0x00);         
    drv_write(0x0C, 0x01);         
}

void SysHaptic::playTick()      { playWaveform(10); } 
void SysHaptic::playConfirm()   { playWaveform(1);  } 
void SysHaptic::playBack()      { playWaveform(15); } 
void SysHaptic::playCoinHeads() { playWaveform(24); } 
void SysHaptic::playCoinTails() { playWaveform(27); } 
void SysHaptic::playAlert()     { playWaveform(64); } 

void SysHaptic::playDecodeSuccess() {
    if (!sysConfig.haptic_enable) return;
    drv_write(0x04, 10);          
    drv_write(0x05, 0x80 | 10);   
    drv_write(0x06, 10);          
    drv_write(0x07, 0x80 | 10);   
    drv_write(0x08, 10);          
    drv_write(0x09, 0x80 | 10);   
    drv_write(0x0A, 1);           
    drv_write(0x0B, 0x00);        
    drv_write(0x0C, 0x01);        
}
void SysHaptic_Sleep() {
    // 0x5A是震动芯片地址，0x01是模式寄存器，0x40代表Standby(待机)
    Wire1.beginTransmission(0x5A);
    Wire1.write(0x01);
    Wire1.write(0x40);
    Wire1.endTransmission();
}

void SysHaptic_Wakeup() {
    // 唤醒恢复到正常模式(0x00)
    Wire1.beginTransmission(0x5A);
    Wire1.write(0x01);
    Wire1.write(0x00);
    Wire1.endTransmission();
}