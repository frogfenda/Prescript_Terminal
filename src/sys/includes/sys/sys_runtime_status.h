/*
【模块职责】运行状态聚合接口。把 NFC 伪装、TT2 倒计时、跨核心推送请求整理成 HUD 和 AppManager 可读取的稳定状态。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>

struct SysHudStatus
{
    bool nfc_active;
    int  nfc_remaining_sec;
    bool countdown_active;
    int  countdown_remaining_sec;
    uint8_t minute_bucket;
};

// 【接口说明】获取菜单 HUD 需要显示的所有运行状态。
SysHudStatus SysRuntime_GetHudStatus();
bool SysRuntime_HudStatusChanged();

void SysRuntime_RequestPushNotify();
// 【接口说明】主循环消费并清除随机指令请求。
bool SysRuntime_ConsumePushNotifyRequest();
