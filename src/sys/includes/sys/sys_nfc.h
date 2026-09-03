/*
【模块职责】FM17550 NFC 系统服务接口。实体卡由后台任务常扫，读取到的 Prescript 文本命令进入
与 BLE 相同的主循环队列；休眠接口负责协作式暂停和射频关闭。
【能力边界】当前产品功能已移除手机模拟卡，相关查询签名只供现有 HUD/App 安全降级。
*/
#pragma once

#include <Arduino.h>

class SysNFC
{
public:
    // 【接口说明】探测 FM17550 并创建常驻扫描任务；扩展板缺席不会阻止系统启动。
    void begin();

    // 【接口说明】提示后台尽快执行一轮扫描；正常状态本身已经持续轮询。
    void triggerManualScan();
};

extern SysNFC sysNfc;

// 【接口说明】模拟卡功能已从当前产品移除，因此始终返回 false/0，启动请求只打印明确说明。
bool SysNfc_IsEmulating();
void SysNfc_StartEmulation();
void SysNfc_StopEmulation();
int SysNfc_GetEmulationRemainingSeconds();

// 【接口说明】在系统待机前后协作暂停扫描，并通过 FM17550 Soft Power-down 关闭/恢复射频。
void SysNfc_Sleep();
void SysNfc_Wakeup();
