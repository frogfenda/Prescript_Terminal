#include "sys_auto_push.h"
#include "sys_config.h"
#include "app_manager.h"

static uint32_t timer_start = 0;
static uint32_t current_interval_ms = 0;
static bool is_waiting = false;

void SysAutoPush_ResetTimer() {
    if (!sysConfig.auto_push_enable) {
        is_waiting = false;
        return;
    }
    // 换算为毫秒
    uint32_t min_ms = sysConfig.auto_push_min_min * 60000;
    uint32_t max_ms = sysConfig.auto_push_max_min * 60000;
    if (min_ms > max_ms) max_ms = min_ms; // 防呆保护
    
    // 随机抽取下次降临的倒计时！
    current_interval_ms = min_ms + random(max_ms - min_ms + 1);
    timer_start = millis();
    is_waiting = true;
}

void SysAutoPush_Init() {
    SysAutoPush_ResetTimer();
}

void SysAutoPush_Update() {
    // 检查是否在等待中，以及总开关是否开启
    if (!is_waiting || !sysConfig.auto_push_enable) return;
    
    // 倒计时结束，强行拉起警报！
    if (millis() - timer_start >= current_interval_ms) {
        SysAutoPush_ResetTimer(); // 立刻重新抽签决定下一次的触发时间
        
        // 【核心解耦】：直接竖起跨核旗帜，让主干管家去拉起那个极其震撼的闪烁警报！
        extern volatile bool g_cross_core_trigger_push;
        g_cross_core_trigger_push = true; 
    }
}

// 留给未来手机端调用的终极接口
void SysAutoPush_UpdateConfig(bool enable, uint32_t min_m, uint32_t max_m) {
    sysConfig.auto_push_enable = enable;
    sysConfig.auto_push_min_min = min_m;
    sysConfig.auto_push_max_min = max_m;
    sysConfig.save();
    SysAutoPush_ResetTimer(); // 重新洗牌计时器
}