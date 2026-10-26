/*
【模块职责】采集菜单 HUD 的运行态快照。
【线程约束】只在 Arduino 主循环调用；不绘制、不写文件、不创建任务。
*/
#include "sys/sys_hud.h"

#include <time.h>

#include "apps/app_countdown.h"
#include "sys/app_manager.h"
#include "sys/sys_nfc.h"
#include "sys/sys_power.h"
#include "sys/sys_time.h"
#include "sys/sys_clock_rules.h"

SysHudStatus SysHud_GetRuntimeStatus()
{
    const int nfc_remaining = SysNfc_GetEmulationRemainingSeconds();
    const int countdown_remaining = Countdown_GetRemainingSeconds();

    SysHudStatus status = {};
    status.nfc_active = nfc_remaining > 0;
    status.nfc_remaining_sec = nfc_remaining;
    status.countdown_active = Countdown_IsActive() && countdown_remaining > 0;
    status.countdown_remaining_sec = countdown_remaining;
    status.pomodoro_active = Pomodoro_GetHudStatus(&status.pomodoro_rest,
                                                   &status.pomodoro_remaining_sec);
    const time_t now = SysTime_NowEpoch();
    status.minute_bucket = static_cast<uint8_t>((now / 60) % 251);
    status.language = appManager.getLanguage();
    SysTime_GetTimeString(status.time_text);
    status.pomodoro_work_max_minutes = SysClockRules::WorkMaxMinutes;

    const SysPowerDisplayState power = sysPower.getDisplayState();
    status.battery_percent = power.battery_percent;
    status.charging = power.charging;
    status.charging_indicator_on = power.charging_indicator_on;
    return status;
}
