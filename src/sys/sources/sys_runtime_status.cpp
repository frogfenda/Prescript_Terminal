// 文件：src/sys/sys_runtime_status.cpp
#include "sys/sys_runtime_status.h"
#include "sys/sys_nfc.h"
#include "sys/sys_time.h"
#include "apps/app_countdown.h"
#include <time.h>

/*
 * 跨核心推送请求标记。
 * 当前用于 BLE Core 0 的即时推送捷径：后台任务不直接切 App，
 * 而是设置这个标记，由 AppManager 主循环安全消费。
 */
static volatile bool s_push_notify_requested = false;

/*
 * HUD 上一次的状态缓存。
 * AppMenuBase 每轮会调用 SysRuntime_HudStatusChanged()，
 * 如果这里判断没有变化，就不会重画整个菜单 HUD。
 */
static bool s_has_last = false;
static SysHudStatus s_last_status = {false, -1, false, -1, 255};

/**
 * 计算当前分钟桶。
 *
 * 菜单 HUD 只需要知道“分钟是否变化”，不需要每轮格式化时间字符串。
 * 原来这里间接调用 SysTime_GetTimeString()，在 NTP 未同步时可能触发 getLocalTime 等待，
 * 导致开机联网失败后，所有继承 AppMenuBase 的滚轮菜单明显变慢。
 *
 * 现在通过唯一时间服务读取 epoch：
 * - 不阻塞；
 * - 每分钟变化一次；
 * - 即使未联网，也会按本地系统时钟推进。
 */
static uint8_t _currentMinuteBucket()
{
    time_t now = SysTime_NowEpoch();

    /*
     * 只关心分钟变化，用 now / 60 即可。
     * 取模 251 是为了让结果放进 uint8_t，同时避开 255 这个初始化哨兵值。
     */
    return (uint8_t)((now / 60) % 251);
}

/**
 * 汇总 HUD 需要显示的运行状态。
 *
 * 左侧 HUD 当前显示：
 * - NFC 伪装倒计时 BUS；
 * - TT2 倒计时 TMR；
 * - 当前分钟桶，用于判断时间显示是否需要刷新。
 *
 * 这个函数不直接画 UI，只给 ui_hud / AppMenuBase 提供状态快照。
 */
SysHudStatus SysRuntime_GetHudStatus()
{
    int nfc_remaining = SysNfc_GetEmulationRemainingSeconds();
    int countdown_remaining = Countdown_GetRemainingSeconds();

    SysHudStatus status;
    status.nfc_active = nfc_remaining > 0;
    status.nfc_remaining_sec = nfc_remaining;
    status.countdown_active = Countdown_IsActive() && countdown_remaining > 0;
    status.countdown_remaining_sec = countdown_remaining;
    status.minute_bucket = _currentMinuteBucket();

    return status;
}

/**
 * 判断 HUD 状态是否变化。
 *
 * AppMenuBase 每轮调用这个函数：
 * - 返回 true：菜单需要重画，因为时间/NFC/倒计时显示变化；
 * - 返回 false：HUD 没变，菜单不用为了 HUD 重画。
 *
 * 这里必须保持轻量，不能调用带等待的网络/时间函数。
 */
bool SysRuntime_HudStatusChanged()
{
    SysHudStatus now = SysRuntime_GetHudStatus();

    bool changed = !s_has_last ||
                   now.nfc_active != s_last_status.nfc_active ||
                   now.nfc_remaining_sec != s_last_status.nfc_remaining_sec ||
                   now.countdown_active != s_last_status.countdown_active ||
                   now.countdown_remaining_sec != s_last_status.countdown_remaining_sec ||
                   now.minute_bucket != s_last_status.minute_bucket;

    s_last_status = now;
    s_has_last = true;

    return changed;
}

/**
 * 请求 AppManager 在主循环中弹出推送通知。
 *
 * 这个函数用于后台模块跨上下文通知 UI：
 * 后台模块只置位，不直接切换 App，避免跨任务直接操作页面栈。
 */
void SysRuntime_RequestPushNotify()
{
    s_push_notify_requested = true;
}

/**
 * AppManager 消费一次推送请求。
 *
 * 返回 true 表示本轮需要启动 PushNotify；
 * 返回 false 表示没有待处理请求。
 */
bool SysRuntime_ConsumePushNotifyRequest()
{
    if (!s_push_notify_requested)
        return false;

    s_push_notify_requested = false;
    return true;
}
