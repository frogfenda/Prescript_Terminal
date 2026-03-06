// 文件：src/sys/sys_config.h
#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>

// 【新增】：番茄钟预设结构体
struct PomodoroPreset {
    String name;
    uint32_t work_min;
    uint32_t rest_min;
};

class SysConfig {
public:
    String wifi_ssid;
    String wifi_pass;
    uint8_t language;       
    uint32_t sleep_time_ms; 

    bool auto_push_enable;
    uint32_t auto_push_min_min; 
    uint32_t auto_push_max_min; 

    // 【新增】：番茄钟硬盘数据
    uint8_t pomodoro_current_idx; // 当前选中的预设编号
    PomodoroPreset pomodoro_presets[5]; // 5个档位的预设

    void load();
    void save();
};

extern SysConfig sysConfig;
#endif