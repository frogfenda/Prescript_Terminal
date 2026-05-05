// 文件：src/sys/sys_nfc.h
#pragma once
#include <Arduino.h>

class SysNFC
{
public:
    void begin();
    void triggerManualScan();
};

extern SysNFC sysNfc;

bool SysNfc_IsEmulating();
void SysNfc_StartEmulation();
void SysNfc_StopEmulation();
int SysNfc_GetEmulationRemainingSeconds();
void SysNfc_Sleep();
void SysNfc_Wakeup();