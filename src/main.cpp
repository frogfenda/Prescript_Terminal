// 文件：src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "sys_config.h"  
#include "sys_time.h" 
#include "sys_network.h" 
#include "sys_auto_push.h" 
#include "sys_ble.h" 
#include "hal.h"
#include "app_manager.h"

void setup() {
    // 保持最低功耗主频
    setCpuFrequencyMhz(80);

    Serial.begin(115200);
    Serial.print("[Main] System Booting on Core: ");
    Serial.println(xPortGetCoreID()); 

    // 初始化时确保射频模块彻底断电
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF); 

    sysConfig.load();
    SysTime_Init();
    HAL_Init();
    appManager.begin();

    SysAutoPush_Init(); 
    SysBLE_Init(); 

    // 【完美恢复】：触发“阅后即焚”式的开机静默对时！
    // 它会在后台默默打开WiFi，抓取到NTP时间的毫秒级瞬间，立刻掐断WiFi电源。
    // 全程无需你干预，既保证了时间精准，又保住了系统的冰凉温度。
    Network_AutoSyncTask(); 
}

void loop() {
    SysAutoPush_Update(); 
    appManager.run();

    // 保持系统呼吸，防止核心死锁发烫
    delay(1); 
}