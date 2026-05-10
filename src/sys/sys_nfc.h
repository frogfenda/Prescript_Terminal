/*
【模块职责】PN532 NFC 接口。提供主动读卡、60 秒手机伪装、剩余时间查询，以及休眠/唤醒控制。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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

// 【接口说明】返回 PN532 是否处于手机靶卡伪装模式，并在超时后自动结束该状态。
bool SysNfc_IsEmulating();
void SysNfc_StartEmulation();
// 【接口说明】提前结束靶卡伪装，后台任务检测结束时间后复位 PN532 并恢复读卡。
void SysNfc_StopEmulation();
int SysNfc_GetEmulationRemainingSeconds();
// 【接口说明】暂停 NFC 后台任务并拉低 RESET 引脚，同时 hold GPIO 减少休眠漏电。
void SysNfc_Sleep();
void SysNfc_Wakeup();
