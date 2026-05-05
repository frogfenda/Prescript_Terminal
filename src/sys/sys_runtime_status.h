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

SysHudStatus SysRuntime_GetHudStatus();
bool SysRuntime_HudStatusChanged();

void SysRuntime_RequestPushNotify();
bool SysRuntime_ConsumePushNotifyRequest();
