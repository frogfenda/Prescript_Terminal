// 文件：src/sys/sys_nfc.h
#pragma once
#include <Arduino.h>

class SysNFC {
public:
    void begin();
    
    // 留给后续“低功耗手动唤醒”的快捷接口
    void triggerManualScan(); 
};

extern SysNFC sysNfc;