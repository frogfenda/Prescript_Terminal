/*
【模块职责】汇总菜单 HUD 所需的只读系统状态。

本模块只采集系统、计时和电源状态，不绘制屏幕；UI 层接收 SysHudStatus 后负责排版和绘制。
*/
#pragma once

#include <Arduino.h>
#include "lang/terminal_lang.h"

struct SysHudStatus
{
    bool nfc_active = false;
    int nfc_remaining_sec = 0;
    bool countdown_active = false;
    int countdown_remaining_sec = 0;
    bool pomodoro_active = false;
    bool pomodoro_rest = false;
    int pomodoro_remaining_sec = 0;
    uint8_t minute_bucket = 0;

    int battery_percent = 100;
    bool charging = false;
    bool charging_indicator_on = false;
    SystemLang_t language = LANG_ZH;
    char time_text[10] = {};
    int pomodoro_work_max_minutes = 99;
};

/** 获取当前 HUD 所需的只读快照；不会推进计时、网络或页面状态。 */
SysHudStatus SysHud_GetRuntimeStatus();
