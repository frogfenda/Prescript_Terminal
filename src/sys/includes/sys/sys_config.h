/*
【模块职责】运行配置数据结构。集中保存 WiFi、语言、休眠、音量、震动、日程、闹钟、硬币、番茄钟、抽卡统计和特殊指令进度。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_config.h (覆盖整个文件)
#ifndef __SYS_CONFIG_H
#define __SYS_CONFIG_H
#include <Arduino.h>
#include "sys/sys_constants.h"
#include "lang/terminal_lang.h"

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
    int coin_count; // 硬币数量（1 到 PrescriptConst::MAX_COIN_COUNT）
    int coin_type;  // 【新增】：硬币型号 (0:经典金, 1:狂气红, 2:沉稳绿)
};
// 声明全局实例化对象
struct CoinPreset {
    String name;      // 技能名
    int base_power;   // 基础点数
    int coin_power;   // 硬币点数
    int coin_count;   // 硬币数量（1 到 PrescriptConst::MAX_COIN_COUNT）
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
    /*
     * 时间系统配置。
     * time_auto_resync：是否允许 Network_Update() 在设备运行中周期性启动轻量 NTP 校时。
     * time_resync_interval_min：周期校时间隔，单位分钟；当前时间设置 UI 只允许 5/15/30/60。
     * time_saved_epoch_valid / time_saved_epoch_utc：最近一次网络对时成功后保存的 UTC epoch。
     *
     * 注意：保存的 epoch 只作为下次开机的兜底显示时间，不能代表断电期间真实经过了多久；
     * 所以 SysTime_Init 会用它设置一个非 1970 的默认时间，但不会把它当成本次网络对时成功。
     */
    bool time_auto_resync;
    uint16_t time_resync_interval_min;
    bool time_saved_epoch_valid;
    uint32_t time_saved_epoch_utc;
    CoinSaveData coin_data;
    uint8_t pomodoro_current_idx;
    PomodoroPreset pomodoro_presets[PrescriptConst::MAX_POMODORO_PRESETS];
    GachaStatsData gacha_stats; // <--- 【新增】：抽卡统计数据全局接入口
    CoinPreset coin_presets[PrescriptConst::MAX_COIN_PRESETS];
    int coin_preset_count = 0;
    uint8_t prescript_target_count = 0;
    String prescript_targets[PrescriptConst::MAX_PRESCRIPT_TARGETS];
    String current_prescript_target;
    uint8_t alarm_count;
    AlarmPreset alarms[PrescriptConst::MAX_ALARMS];
    uint8_t decode_anim_style; // 【新增】：解码动画样式 (0:动画一, 1:动画二)
    // 【新增】：日程表硬盘数据
    uint8_t schedule_count;
    ScheduleItem schedules[PrescriptConst::MAX_SCHEDULES]; // 最多日程数由 sys_constants.h 统一定义
    uint8_t volume;             // 【新增】：系统全局音量 (0~10)
    bool haptic_enable;         // 震动总开关
    uint8_t haptic_intensity;   // 震动强度 (1=弱, 2=中, 3=强)
    uint8_t nfc_mode;
    uint32_t special_toggles; // 开关位掩码 (默认 0xFFFFFFFF 全开)
    uint8_t char_progress[PrescriptConst::MAX_CHAR_CHAINS]; // 人物链条进度表 (最多支持 8 个特殊人物)
    // 【接口说明】读取公共配置和当前语言配置，并填充 SysConfig 字段。
    void load();
    void save();
    // 【接口说明】只保存设备级公共配置，例如 WiFi、音量、休眠、网络校时和当前语言。
    void saveCommon();
    // 【接口说明】读取/保存指定语言的内容配置，例如闹钟、日程、使用者、特异点进度和文本预设。
    void loadLanguageProfile(SystemLang_t lang);
    void saveLanguageProfile(SystemLang_t lang);
};

extern SysConfig sysConfig;
#endif
