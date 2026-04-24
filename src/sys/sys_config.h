// 文件：src/sys/sys_config.h (覆盖整个文件)
#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>

struct PomodoroPreset
{
    String name;
    uint32_t work_min;
    uint32_t rest_min;
};

struct AlarmPreset
{
    bool is_active;
    uint8_t hour;
    uint8_t min;
    String name;
    String prescript;
};

// 【新增】：日程表结构体
struct ScheduleItem
{
    uint32_t target_time; // 触发的时间戳
    uint32_t expire_time; // 过期的时间戳 (用于24小时销毁)
    String title;         // 日程标题
    String prescript;     // 为空则代表"随机都市指令"
    bool is_expired;      // 是否已过期
    bool is_restored;     // 是否是恢复的日程
    bool is_hidden;       // 【新增】：是否为隐藏日程（不在UI显示）
};

struct CoinSaveData
{
    int mode;       // 运行模式：0自动, 1手动
    int sanity;     // 理智波动：-45 到 45
    int coin_count; // 【新增】：硬币数量 (1 到 4)
    int coin_type;  // 【新增】：硬币型号 (0:经典金, 1:狂气红, 2:沉稳绿)
};
// 声明全局实例化对象
struct CoinPreset {
    String name;      // 技能名
    int base_power;   // 基础点数
    int coin_power;   // 硬币点数
    int coin_count;   // 硬币数量
    String coin_colors; // 【核心升级】：从单数字变成材质字符串序列，如 "1102"
};
struct GachaStatsData {
    uint32_t total;
    uint32_t s3;
    uint32_t s2;
    uint32_t s1;
    uint32_t w3;
    uint32_t w2; // 严谨遵守设定：瓦夜无 1 星
};
class SysConfig
{
public:
    String wifi_ssid;
    String wifi_pass;
    uint8_t language;
    uint32_t sleep_time_ms;
    uint32_t true_sleep_time_ms; // 【新增】：在待机画面停留多久后【真正休眠】
    bool auto_push_enable;
    uint32_t auto_push_min_min;
    uint32_t auto_push_max_min;
    CoinSaveData coin_data;
    uint8_t pomodoro_current_idx;
    PomodoroPreset pomodoro_presets[5];
    GachaStatsData gacha_stats; // <--- 【新增】：抽卡统计数据全局接入口
    CoinPreset coin_presets[10];
    int coin_preset_count = 0;
    uint8_t alarm_count;
    AlarmPreset alarms[10];
    uint8_t decode_anim_style; // 【新增】：解码动画样式 (0:动画一, 1:动画二)
    // 【新增】：日程表硬盘数据
    uint8_t schedule_count;
    ScheduleItem schedules[15]; // 最多 15 个日程
    uint8_t volume;             // 【新增】：系统全局音量 (0~10)
    bool haptic_enable;         // 震动总开关
    uint8_t haptic_intensity;   // 震动强度 (1=弱, 2=中, 3=强)
    uint8_t nfc_mode;
    uint32_t special_toggles; // 开关位掩码 (默认 0xFFFFFFFF 全开)
    uint8_t char_progress[8]; // 人物链条进度表 (最多支持 8 个特殊人物)
    void load();
    void save();
};

extern SysConfig sysConfig;
#endif