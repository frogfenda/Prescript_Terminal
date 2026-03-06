#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>

class SysConfig {
public:
    String wifi_ssid;
    String wifi_pass;
    uint8_t language;       
    uint32_t sleep_time_ms; 

    // 【新增】：都市意志自动推送参数
    bool auto_push_enable;
    uint32_t auto_push_min_min; // 最短时间(分钟)
    uint32_t auto_push_max_min; // 最长时间(分钟)

    void load();
    void save();
};

extern SysConfig sysConfig;
#endif