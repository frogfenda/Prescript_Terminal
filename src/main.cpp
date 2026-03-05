#include <Arduino.h>
#include <WiFi.h>
#include "hal.h"
#include "app_manager.h"

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_OFF);
    delay(10);

    Serial.println(">>> 启动系统大管家 <<<");
    
    HAL_Init(); // 初始化底层硬件

    // 开机直接启动待机应用
    appManager.launchApp(appStandby);
}

void loop() {
    // 永远只跑管家，管家去调度应用
    appManager.run();
    delay(10);
}