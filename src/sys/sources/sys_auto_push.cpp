/*
【模块职责】自动推送实现。每次重置时在最小/最大分钟之间随机生成下次触发时间，并把截止时间登记到统一休眠调度器。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys/sys_auto_push.h"
#include "sys/sys_config.h"
#include "sys/sys_reminder.h"
#include "sys/sys_sleep_scheduler.h"
#include <limits.h>

static uint32_t timer_start = 0;
static uint32_t current_interval_ms = 0;
static bool is_waiting = false;

void SysAutoPush_ResetTimer() {
    if (!sysConfig.auto_push_enable) {
        is_waiting = false;
        SysSleep_Cancel(SysSleepSource::AutoPush);
        return;
    }

    /* 相对截止时间必须位于 INT32_MAX 毫秒内，确保休眠调度和运行态判断使用同一回绕安全范围。 */
    constexpr uint32_t MAX_INTERVAL_MIN = (uint32_t)INT32_MAX / 60000UL;
    uint32_t min_minutes = min(sysConfig.auto_push_min_min, MAX_INTERVAL_MIN);
    uint32_t max_minutes = min(sysConfig.auto_push_max_min, MAX_INTERVAL_MIN);
    uint32_t min_ms = min_minutes * 60000UL;
    uint32_t max_ms = max_minutes * 60000UL;
    if (min_ms > max_ms) max_ms = min_ms; // 防呆保护
    
    // 随机抽取下次降临的倒计时！
    current_interval_ms = min_ms + random(max_ms - min_ms + 1);
    timer_start = millis();
    is_waiting = true;
    SysSleep_ScheduleAfterMs(
        SysSleepSource::AutoPush,
        current_interval_ms,
        SysSleepWakeAction::Foreground);
}

void SysAutoPush_Init() {
    SysAutoPush_ResetTimer();
}

void SysAutoPush_Update() {
    // 检查是否在等待中，以及总开关是否开启
    if (!is_waiting) return;
    if (!sysConfig.auto_push_enable) {
        is_waiting = false;
        SysSleep_Cancel(SysSleepSource::AutoPush);
        return;
    }
    
    // 倒计时结束，强行拉起警报！
    if (millis() - timer_start >= current_interval_ms) {
        /* 队列满时保留到期状态继续重试；成功入队后才重新抽取下一次间隔。 */
        if (!SysReminder_Submit(SysReminderKind::Random, nullptr, true))
            return;
        SysAutoPush_ResetTimer();
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
