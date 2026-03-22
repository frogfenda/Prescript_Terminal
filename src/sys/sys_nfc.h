// 文件：src/sys/sys_nfc.h
#pragma once
#include <Arduino.h>

class SysNFC
{
public:
    void begin();
    void triggerManualScan();
    
    // 【新增】：开启伪装模式
    void SysNfc_StartEmulation(); 
};

extern SysNFC sysNfc;

// 【新增】：向全系统暴露的倒计时状态
extern bool g_nfc_is_emulating;
extern uint32_t g_nfc_emu_end_time;