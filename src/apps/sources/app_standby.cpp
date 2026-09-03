/*
【模块职责】待机页。显示 standby.bin，处理空闲后 Light Sleep，并响应按键、RTC INT# 或 ESP 定时器唤醒。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_standby.cpp
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_config.h"
#include "hal/hal.h"
#include "sys/sys_haptic.h"
#include "sys/sys_audio.h"
#include "sys/sys_nfc.h"
#include "sys/sys_time.h"
#include "sys/sys_calendar.h"
#include "sys/sys_sleep_scheduler.h"
#include "sys/sys_motion.h"
#include "sys/sys_mag.h"

class AppStandby : public AppBase
{
private:
    uint32_t enter_time;
    bool is_sleeping;

    /**
     * 计算本轮 Light Sleep 最长持续时间。
     * 所有业务截止时间和每小时 RTC 维护都由 SysSleepScheduler 聚合，Standby 只消费最近计划。
     */
    uint64_t nextTimerWakeupUs()
    {
        SysSleepPlan plan = {};
        if (!SysSleep_GetPlan(&plan))
            return 0;
        uint64_t delay_ms = plan.delay_ms;

        /* ESP-IDF 定时唤醒使用微秒；至少留 100ms，避免临界点计算造成立即唤醒死循环。 */
        if (delay_ms < 100)
            delay_ms = 100;
        return delay_ms * 1000ULL;
    }

    /** RTC 同步后重新计算计划；前台来源到期时必须完整恢复外设并交回主循环。 */
    bool foregroundWakeIsDue()
    {
        SysSleepPlan plan = {};
        if (!SysSleep_GetPlan(&plan))
            return false;
        return plan.delay_ms == 0 && plan.action == SysSleepWakeAction::Foreground;
    }

public:
    // 【函数说明】进入待机页时绘制 standby.bin，重置睡眠计时，等待用户按键或到达真休眠时间。
    void onCreate() override
    {
        HAL_Screen_Clear();
        HAL_Screen_DrawStandbyImage();
        HAL_Screen_Update();
        enter_time = millis();
        is_sleeping = false;
    }

    // 【函数说明】待机页循环：达到 true_sleep_time_ms 后执行 Light Sleep，任一实体按键唤醒后恢复并重绘待机图。
    void onLoop() override
    {
        // 【防呆检查】：如果设置为“永不休眠”，直接退出，不计时！
        if (sysConfig.true_sleep_time_ms == PrescriptConst::NEVER_SLEEP_MS)
            return;

        // 【核心休眠触发逻辑】：精准使用你原本的变量
        if (!is_sleeping && (millis() - enter_time > sysConfig.true_sleep_time_ms))
        {
            /* 网络事务、NFC 靶卡等模块统一登记 blocker；Standby 不直接了解各模块状态。 */
            if (!SysSleep_CanEnter())
                return;

            is_sleeping = true;

            // --- 1. UI 准备：休眠前清空屏幕，防止醒来闪烁旧画面 ---
            HAL_Screen_Clear();
            HAL_Screen_Update();

            // --- 2. 模块级休眠：App 管家下发指令 ---
            SysHaptic_Sleep();
            SysAudio_Sleep();
            SysNfc_Sleep();
            SysMotion_Sleep();
            SysMag_Sleep();

            // --- 3. 硬件级休眠：调用最新拆分的 HAL 底层 ---
            HAL_Sleep_Enter_Prepare(); // 引脚锁定，驱动 IC 挂起

            HALSleepWakeReason wake_reason = HALSleepWakeReason::Error;
            while (true)
            {
                wake_reason = HAL_Sleep_Start(nextTimerWakeupUs());

                if (wake_reason == HALSleepWakeReason::Button)
                {
                    SysTime_RefreshFromRtc(SysTimeRefreshReason::Wakeup);
                    break;
                }

                if (wake_reason == HALSleepWakeReason::Rtc)
                {
                    /*
                     * PCF8563 AF 为锁存低电平。先恢复唯一可信时间，再让 SysCalendar 在外设仍休眠时
                     * 处理到期业务、确认 AF 并写回下一槽；只有真实业务到期才恢复前台。
                     */
                    SysTime_RefreshFromRtc(SysTimeRefreshReason::Wakeup);
                    if (SysCalendar_ServiceSleepWake())
                        break;

                    // 月初检查点或无业务的毛刺唤醒已经确认 AF 并写好下一槽，外设保持休眠直接续睡。
                    continue;
                }

                if (wake_reason == HALSleepWakeReason::Timer)
                {
                    /* 定时器可能服务每小时维护，也可能服务最近提醒；两种情况都先以 RTC 校准系统。 */
                    SysTime_RefreshFromRtc(SysTimeRefreshReason::Hourly);
                    bool calendar_due = SysCalendar_ServiceSleepWake();
                    if (calendar_due || foregroundWakeIsDue())
                        break;

                    // 纯每小时维护唤醒：屏幕、音频、震动、NFC 保持休眠，直接开始下一轮。
                    continue;
                }

                // HAL 拒绝休眠或返回未知原因时必须恢复外设，不能让设备停在黑屏状态。
                break;
            }

            // --- 4. 唤醒：恢复屏幕与外设，并把本次唤醒按压交给 ButtonEngine 非阻塞吞掉 ---
            HAL_Sleep_Wakeup_Post();
            SysMotion_Wakeup();
            SysMag_Wakeup();

            // --- 5. 业务逻辑恢复 ---
            // 唤醒后仍停留在待机页面，避免重复调用各模块 wakeup。
            HAL_Screen_Update();

            enter_time = millis();
            is_sleeping = false;

            appManager.resetIdleTimer();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    // 【函数说明】亮屏待机时，旋钮主按键或由 AppManager 映射过来的侧键短按都会进入主菜单。
    void onKeyShort() override
    {
        // 因为底层的 HAL_Sleep_Wakeup_Post 已经吞掉了“唤醒那一下”的按键
        // 所以当代码走到这里，说明用户在亮屏待机状态下完成了一次新的主键/侧键短按。
        Feedback_PlayWake();
        appManager.launch(AppId::MainMenu);
    }
    // 【新增】：如果你想手动“点一下”就进待机（休眠），可以加长按逻辑

};

AppStandby instanceStandby;
AppBase *appStandby = &instanceStandby;
