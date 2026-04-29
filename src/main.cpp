#include <Arduino.h>
#include <WiFi.h>
#include "sys_config.h"
#include "sys_time.h"
#include "sys_network.h"
#include "sys_auto_push.h"
#include "sys_ble.h"
#include "sys_fs.h" // <--- 【新增】引入文件系统中枢
#include "hal.h"
#include "app_manager.h"
#include "sys/sys_audio.h"
#include "sys_haptic.h"
#include "sys_nfc.h"
#include "sys_specials.h"
#include "sys_power.h"

void setup()
{
    setCpuFrequencyMhz(80);

    Serial.begin(115200);
    Serial.println("[Main] System Booting...");

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    // 【新增】：在所有系统初始化前，挂载内部数据库
    SysFS_Init();
    SysFS_Load_Prescripts();
    sysConfig.load();
    sysSpecials.begin();
    SysTime_Init();
    sysAudio.begin();
    sysPower.begin();
    HAL_Init();
    appManager.begin();
    extern void SysRouter_Init();
    SysRouter_Init();
    sysHaptic.begin();
    SysAutoPush_Init();
    SysBLE_Init();
    sysNfc.begin();
    Network_Init();
    Network_StartSync();
}

void loop()
{
    SysAutoPush_Update();
    appManager.run();
    delay(1);
}