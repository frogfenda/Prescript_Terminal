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
    // 【降温核心优化 1】：主动降频！
    // 将 ESP32-S3 从默认的 240MHz 降级到 160MHz
    // 对于这类终端机，160MHz 性能完全溢出，但功耗和发热量会大幅度降低
    setCpuFrequencyMhz(160);

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

    // 【降温核心优化 2】：删除了原本在这里强行开启 WiFi 的代码
    // 现在系统开机绝对不会启动电老虎 WiFi，而是由底层的网络调度器按需开启

    SysBLE_Init(); 
}

void loop() {
    SysAutoPush_Update(); 
    appManager.run();

    // 【降温核心优化 3】：加入 FreeRTOS 的系统级“呼吸”机制
    // 仅仅这 1 毫秒的延时，就能让 Core 1 把控制权交还给系统 Idle 任务
    // 让 CPU 获得微秒级的睡眠时间，CPU 占用率直接从 100% 暴降至个位数！
    delay(1); 
}