#include "sys_runtime_status.h"
#include "sys_nfc.h"
#include "sys_time.h"
#include "../apps/app_countdown.h"

static volatile bool s_push_notify_requested = false;
static bool s_has_last = false;
static SysHudStatus s_last_status = {false, -1, false, -1, 255};

static uint8_t _currentMinuteBucket()
{
    char time_str[10];
    SysTime_GetTimeString(time_str);
    // Expected HH:MM. If RTC is not ready, this still produces a stable-ish bucket.
    if (time_str[0] >= '0' && time_str[0] <= '9' && time_str[3] >= '0' && time_str[3] <= '9')
    {
        int h = (time_str[0] - '0') * 10 + (time_str[1] - '0');
        int m = (time_str[3] - '0') * 10 + (time_str[4] - '0');
        return (uint8_t)((h * 60 + m) % 251);
    }
    return (uint8_t)((millis() / 60000UL) % 251);
}

SysHudStatus SysRuntime_GetHudStatus()
{
    int nfc_remaining = SysNfc_GetEmulationRemainingSeconds();
    int countdown_remaining = Countdown_GetRemainingSeconds();
    SysHudStatus status;
    status.nfc_active = nfc_remaining > 0;
    status.nfc_remaining_sec = nfc_remaining;
    status.countdown_active = Countdown_IsActive() && countdown_remaining > 0;
    status.countdown_remaining_sec = countdown_remaining;
    status.minute_bucket = _currentMinuteBucket();
    return status;
}

bool SysRuntime_HudStatusChanged()
{
    SysHudStatus now = SysRuntime_GetHudStatus();
    bool changed = !s_has_last ||
                   now.nfc_active != s_last_status.nfc_active ||
                   now.nfc_remaining_sec != s_last_status.nfc_remaining_sec ||
                   now.countdown_active != s_last_status.countdown_active ||
                   now.countdown_remaining_sec != s_last_status.countdown_remaining_sec ||
                   now.minute_bucket != s_last_status.minute_bucket;
    s_last_status = now;
    s_has_last = true;
    return changed;
}


void SysRuntime_RequestPushNotify()
{
    s_push_notify_requested = true;
}

bool SysRuntime_ConsumePushNotifyRequest()
{
    if (!s_push_notify_requested) return false;
    s_push_notify_requested = false;
    return true;
}
