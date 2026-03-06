// 文件：src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "sys_config.h"  
#include "sys_time.h" 
#include "sys_network.h" 
#include "sys_auto_push.h" // 【新增】
#include "hal.h"
#include "app_manager.h"

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_OFF); 
    sysConfig.load();
    SysTime_Init();
    HAL_Init();
    appManager.begin();
    
    // 开机初始化推送引擎和网络
    SysAutoPush_Init(); // 【新增】
    Network_Connect(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());
}

void loop() {
    Network_Update(); 
    SysAutoPush_Update(); // 【新增】：在后台默默倒计时
    appManager.run();
}