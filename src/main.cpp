#include <Arduino.h>
#include <WiFi.h>
#include "app_fsm.h"

void setup() {
    Serial.begin(115200);
    
    // 关闭 WiFi 加速开机
    WiFi.mode(WIFI_OFF);
    delay(10);

    Serial.println(">>> 正在启动都市指令分配终端 <<<");

    // 启动核心状态机 (里面包含屏幕、旋钮、蜂鸣器的初始化)
    App_FSM_Init();
}

void loop() {
    // 状态机时间片流转，处理 UI 和交互
    App_FSM_Run();
}