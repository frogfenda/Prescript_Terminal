/*
【模块职责】AppId 注册表接口。把“哪个 App 实例对应哪个页面”集中在 app_registry.cpp，避免各个 App 互相 extern 指针。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include "sys/app_base.h"

// Central application index.
// Public code should navigate with AppId instead of sharing raw AppBase* globals.

enum class AppId : uint8_t
{
    Standby,
    MainMenu,
    Prescript,
    Loom,
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
    PrescriptTarget,
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
    GachaStats,

    // 时间设置相关页面：一级时间菜单、当日时分编辑、日期编辑。
    TimeSetting,
    TimeManualSet,
    TimeDateSet,

    // 沉浸式海面流体模拟；追加在枚举末尾，避免改变此前 AppId 的数值。
    Sea,

    // 三模式业力木鱼；继续追加，避免改变已经用于导航和日志的枚举值。
    Karma,

    // 双蛇杖正式应用；继续追加以保持既有 AppId 数值稳定。
    Caduceus,

    // 六动作语义校准页只从系统设置进入；追加在末尾，避免改变此前AppId数值。
    CaduceusActionTest,

    // 固定入口人体坐标漂移调试页；继续追加，保持此前 AppId 数值稳定。
    HumanFrameDriftTest,

    // 独立地磁数据、校准和干扰诊断页；继续追加以保持此前AppId数值稳定。
    MagDiagnostics
};

/** 根据 AppId 返回对应 App 单例指针，AppManager 的 push/launch/replace 会调用它完成页面跳转。 */
AppBase* AppRegistry_Get(AppId id);

/** 返回 AppId 的调试名称，用于串口日志或错误追踪。 */
const char* AppRegistry_Name(AppId id);

/** 安装需要事件订阅或后台 tick 的系统 App。纯导航页面不需要在这里安装。 */
void AppRegistry_InstallSystemApps();
