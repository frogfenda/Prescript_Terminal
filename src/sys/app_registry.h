#pragma once
#include <Arduino.h>
#include "app_base.h"

// Central application index.
// Public code should navigate with AppId instead of sharing raw AppBase* globals.

enum class AppId : uint8_t
{
    Standby,
    MainMenu,
    Prescript,
    SleepSetting,
    NetworkSync,
    SystemSettings,
    WifiConnect,
    CoinFlip,
    CoinQuick,
    CoinSkill,
    CoinSettings,
    CoinPresetEdit,
    Countdown,
    Gacha,
    PushNotify,
    PushSetting,
    Pomodoro,
    PomodoroRun,
    PomodoroPresets,
    PomodoroEdit,
    Alarm,
    AlarmEdit,
    Schedule,
    ScheduleEdit,
    ScheduleExpired,
    AnimSetting,
    PrescriptList,
    VolumeSetting,
    GachaStats
};

AppBase* AppRegistry_Get(AppId id);
const char* AppRegistry_Name(AppId id);
void AppRegistry_InstallSystemApps();
