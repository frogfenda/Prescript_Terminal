// 文件：src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "sys_config.h"  
#include "sys_time.h" 
#include "sys_network.h" 
#include "sys_auto_push.h" 
#include "sys_ble.h" // 【新增】：引入蓝牙模块
#include "hal.h"
#include "app_manager.h"

void setup() {
    Serial.begin(115200);
    
    // 打印当前主启动代码运行的核
    Serial.print("[Main] System Booting on Core: ");
    Serial.println(xPortGetCoreID()); 

    WiFi.mode(WIFI_OFF); 
    sysConfig.load();
    SysTime_Init();
    HAL_Init();
    appManager.begin();

    SysAutoPush_Init(); 
    Network_Connect(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());

    // 【新增】：点火！将底层协议剥离给 Core 0
    SysBLE_Init(); 
}

void loop() {
    // 这里的 loop() 其实就是跑在 Core 1 上的无限循环
    Network_Update(); 
    SysAutoPush_Update(); 
    appManager.run();
}