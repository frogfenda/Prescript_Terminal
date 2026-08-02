/*
【模块职责】App 实例集中定义与查找表。所有页面对象在这里静态创建，AppManager 通过 AppId 取指针并调用生命周期。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys/app_registry.h"
#include "sys/app_manager.h"

// Concrete app singletons still live in their own app_xxx.cpp files.
// Keep their symbols private to this registry instead of exposing them project-wide.
extern AppBase *appStandby;
extern AppBase *appMainMenu;
extern AppBase *appPrescript;
extern AppBase *appOracle;
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
extern AppBase *appPrescriptTarget;
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
extern AppBase *appSea;
extern AppBase *appKarma;
extern AppBase *appCaduceus;
extern AppBase *appCaduceusActionTest;

// 时间设置相关 App 在 app_time_setting.cpp 中定义。
extern AppBase *appTimeSetting;
extern AppBase *appTimeManualSet;
extern AppBase *appTimeDateSet;

AppBase* AppRegistry_Get(AppId id)
{
    switch (id)
    {
        case AppId::Standby: return appStandby;
        case AppId::MainMenu: return appMainMenu;
        case AppId::Prescript: return appPrescript;
        case AppId::Loom: return appOracle;
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
        case AppId::PrescriptTarget: return appPrescriptTarget;
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
        case AppId::TimeSetting: return appTimeSetting;
        case AppId::TimeManualSet: return appTimeManualSet;
        case AppId::TimeDateSet: return appTimeDateSet;
        case AppId::Sea: return appSea;
        case AppId::Karma: return appKarma;
        case AppId::Caduceus: return appCaduceus;
        case AppId::CaduceusActionTest: return appCaduceusActionTest;
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
        case AppId::Loom: return "Loom";
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
        case AppId::PrescriptTarget: return "PrescriptTarget";
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
        case AppId::TimeSetting: return "TimeSetting";
        case AppId::TimeManualSet: return "TimeManualSet";
        case AppId::TimeDateSet: return "TimeDateSet";
        case AppId::Sea: return "Sea";
        case AppId::Karma: return "Karma";
        case AppId::Caduceus: return "Caduceus";
        case AppId::CaduceusActionTest: return "CaduceusActionTest";
        default: return "Unknown";
    }
}

void AppRegistry_InstallSystemApps()
{
    /*
     * 这里只安装需要事件订阅或后台 tick 的 App。
     * TimeSetting / TimeManualSet / TimeDateSet 只是用户进入时才运行的导航页面，
     * 没有事件订阅，也不需要后台检查，所以不安装到后台 App 列表。
     */
    appManager.installApp(AppId::Schedule);
    appManager.installApp(AppId::Alarm);
    appManager.installApp(AppId::Pomodoro);
    appManager.installApp(AppId::PrescriptList);
    appManager.installApp(AppId::Countdown);
    appManager.installApp(AppId::Gacha);
    appManager.installApp(AppId::GachaStats);
    appManager.installApp(AppId::PushNotify);
}
