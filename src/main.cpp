#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "app_fsm.h"

void setup() {
    // 关闭 WiFi 加速开机
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1);

    // 启动状态机
    App_FSM_Init();
}

void loop() {
    // 状态机时间片流转
    App_FSM_Run();
}