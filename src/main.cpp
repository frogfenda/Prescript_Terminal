// 文件：src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "sys_config.h"  
#include "sys_time.h" 
#include "hal.h"
#include "app_manager.h"

void setup() {
    Serial.begin(115200);
    
    // 1. 初始化底层硬件与环境
    WiFi.mode(WIFI_OFF); 
    sysConfig.load();
    SysTime_Init();
    HAL_Init();
    
    // 2. 启动系统大管家
    appManager.begin();
}

void loop() {
    // 3. 将单片机的所有算力交给大管家调度
    appManager.run();
}