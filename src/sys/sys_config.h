// 文件：src/sys/sys_config.h (覆盖整个文件)
#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>

struct PomodoroPreset {
    String name;
    uint32_t work_min;
    uint32_t rest_min;
};

struct AlarmPreset {
    bool is_active;    
    uint8_t hour;      
    uint8_t min;       
    String name;       
    String prescript;  
};

// 【新增】：日程表结构体
struct ScheduleItem {
    uint32_t target_time; // 触发的时间戳
    uint32_t expire_time; // 过期的时间戳 (用于24小时销毁)
    String title;         // 日程标题
    String prescript;     // 为空则代表"随机都市指令"
    bool is_expired;      // 是否已过期
    bool is_restored;     // 是否是恢复的日程
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

    uint8_t alarm_count; 
    AlarmPreset alarms[10]; 

    // 【新增】：日程表硬盘数据
    uint8_t schedule_count; 
    ScheduleItem schedules[15]; // 最多 15 个日程

    void load();
    void save();
};

extern SysConfig sysConfig;
#endif