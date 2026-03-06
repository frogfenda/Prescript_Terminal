// 文件：src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "sys_config.h"  // 【关键】：必须在头部引入我们新写的配置中心
#include "hal.h"
#include "app_manager.h"

void setup() {
    Serial.begin(115200);
    
    // 初始关闭 WiFi，极限省电！(网络模块启动时会自动唤醒)
    WiFi.mode(WIFI_OFF); 
    
    // 开机第一时间，从 Flash 硬盘中加载用户配置
    sysConfig.load();
    
    HAL_Init();
    appManager.begin();
}

void loop() {
    appManager.update();
}