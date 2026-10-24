/*
【模块职责】运行状态聚合接口。把 NFC、倒计时、番茄应用只读状态与跨核心推送请求整理为 HUD / AppManager 快照。
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
    bool pomodoro_active;
    bool pomodoro_rest;
    int  pomodoro_remaining_sec;
    uint8_t minute_bucket;
};

// 【接口说明】主循环只读获取菜单 HUD 状态；不推进业务、不访问外设或持久化运行状态。
SysHudStatus SysRuntime_GetHudStatus();
bool SysRuntime_HudStatusChanged();

