// 文件：src/sys/sys_config.h
#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>

struct PomodoroPreset {
    String name;
    uint32_t work_min;
    uint32_t rest_min;
};

// 【新增】：闹钟结构体
struct AlarmPreset {
    bool is_active;    // 是否开启
    uint8_t hour;      // 时
    uint8_t min;       // 分
    String name;       // 闹钟名
    String prescript;  // 弹出指令
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

    uint8_t pomodoro_current_idx; 
    PomodoroPreset pomodoro_presets[5]; 

    // 【新增】：闹钟硬盘数据
    uint8_t alarm_count; // 当前闹钟数量
    AlarmPreset alarms[10]; // 最多 10 个闹钟

    void load();
    void save();
};

extern SysConfig sysConfig;
#endif