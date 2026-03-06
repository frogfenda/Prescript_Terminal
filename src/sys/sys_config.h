#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>

class SysConfig {
public:
    String wifi_ssid;
    String wifi_pass;
    uint8_t language;       // 0: 英文, 1: 中文
    uint32_t sleep_time_ms; // 休眠时间

    // 从单片机 Flash 硬盘中读取配置
    void load();
    // 将当前配置写入 Flash 硬盘永久保存
    void save();
};

extern SysConfig sysConfig;
#endif