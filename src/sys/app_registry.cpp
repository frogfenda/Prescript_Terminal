#include "app_registry.h"
#include "app_manager.h"

// Concrete app singletons still live in their own app_xxx.cpp files.
// Keep their symbols private to this registry instead of exposing them project-wide.
extern AppBase *appStandby;
extern AppBase *appMainMenu;
extern AppBase *appPrescript;
extern AppBase *appSleepSetting;
extern AppBase *appNetworkSync;
extern AppBase *appSystemSettings;
extern AppBase *appWifiConnect;
extern AppBase *appCoinFlip;
extern AppBase *appCoinQuick;
extern AppBase *appCoinSkill;
extern AppBase *appCoinSettings;
extern AppBase *appCoinPresetEdit;
extern AppBase *appCountdown;
extern AppBase *appGacha;
extern AppBase *appPushNotify;
extern AppBase *appPushSetting;
extern AppBase *appPomodoro;
extern AppBase *appPomodoroRun;
extern AppBase *appPomodoroPresets;
extern AppBase *appPomodoroEdit;
extern AppBase *appAlarm;
extern AppBase *appAlarmEdit;
extern AppBase *appSchedule;
extern AppBase *appScheduleEdit;
extern AppBase *appScheduleExpired;
extern AppBase *appAnimSetting;
extern AppBase *appPrescriptList;
extern AppBase *appVolumeSetting;
extern AppBase *appGachaStats;

AppBase* AppRegistry_Get(AppId id)
{
    switch (id)
    {
        case AppId::Standby: return appStandby;
        case AppId::MainMenu: return appMainMenu;
        case AppId::Prescript: return appPrescript;
        case AppId::SleepSetting: return appSleepSetting;
        case AppId::NetworkSync: return appNetworkSync;
        case AppId::SystemSettings: return appSystemSettings;
        case AppId::WifiConnect: return appWifiConnect;
        case AppId::CoinFlip: return appCoinFlip;
        case AppId::CoinQuick: return appCoinQuick;
        case AppId::CoinSkill: return appCoinSkill;
        case AppId::CoinSettings: return appCoinSettings;
        case AppId::CoinPresetEdit: return appCoinPresetEdit;
        case AppId::Countdown: return appCountdown;
        case AppId::Gacha: return appGacha;
        case AppId::PushNotify: return appPushNotify;
        case AppId::PushSetting: return appPushSetting;
        case AppId::Pomodoro: return appPomodoro;
        case AppId::PomodoroRun: return appPomodoroRun;
        case AppId::PomodoroPresets: return appPomodoroPresets;
        case AppId::PomodoroEdit: return appPomodoroEdit;
        case AppId::Alarm: return appAlarm;
        case AppId::AlarmEdit: return appAlarmEdit;
        case AppId::Schedule: return appSchedule;
        case AppId::ScheduleEdit: return appScheduleEdit;
        case AppId::ScheduleExpired: return appScheduleExpired;
        case AppId::AnimSetting: return appAnimSetting;
        case AppId::PrescriptList: return appPrescriptList;
        case AppId::VolumeSetting: return appVolumeSetting;
        case AppId::GachaStats: return appGachaStats;
        default: return nullptr;
    }
}

const char* AppRegistry_Name(AppId id)
{
    switch (id)
    {
        case AppId::Standby: return "Standby";
        case AppId::MainMenu: return "MainMenu";
        case AppId::Prescript: return "Prescript";
        case AppId::SleepSetting: return "SleepSetting";
        case AppId::NetworkSync: return "NetworkSync";
        case AppId::SystemSettings: return "SystemSettings";
        case AppId::WifiConnect: return "WifiConnect";
        case AppId::CoinFlip: return "CoinFlip";
        case AppId::CoinQuick: return "CoinQuick";
        case AppId::CoinSkill: return "CoinSkill";
        case AppId::CoinSettings: return "CoinSettings";
        case AppId::CoinPresetEdit: return "CoinPresetEdit";
        case AppId::Countdown: return "Countdown";
        case AppId::Gacha: return "Gacha";
        case AppId::PushNotify: return "PushNotify";
        case AppId::PushSetting: return "PushSetting";
        case AppId::Pomodoro: return "Pomodoro";
        case AppId::PomodoroRun: return "PomodoroRun";
        case AppId::PomodoroPresets: return "PomodoroPresets";
        case AppId::PomodoroEdit: return "PomodoroEdit";
        case AppId::Alarm: return "Alarm";
        case AppId::AlarmEdit: return "AlarmEdit";
        case AppId::Schedule: return "Schedule";
        case AppId::ScheduleEdit: return "ScheduleEdit";
        case AppId::ScheduleExpired: return "ScheduleExpired";
        case AppId::AnimSetting: return "AnimSetting";
        case AppId::PrescriptList: return "PrescriptList";
        case AppId::VolumeSetting: return "VolumeSetting";
        case AppId::GachaStats: return "GachaStats";
        default: return "Unknown";
    }
}

void AppRegistry_InstallSystemApps()
{
    // Apps that subscribe to events or need background ticks register themselves here.
    // Navigation-only apps do not need onSystemInit() during boot.
    appManager.installApp(AppId::Schedule);
    appManager.installApp(AppId::Alarm);
    appManager.installApp(AppId::Pomodoro);
    appManager.installApp(AppId::PrescriptList);
    appManager.installApp(AppId::Countdown);
    appManager.installApp(AppId::Gacha);
    appManager.installApp(AppId::GachaStats);
    appManager.installApp(AppId::PushNotify);
}
