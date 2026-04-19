// 文件：src/apps/app_standby.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h"
#include "hal.h"

class AppStandby : public AppBase
{
private:
    uint32_t enter_time;
    bool is_sleeping;

public:
    void onCreate() override
    {
        HAL_Screen_Clear();
        HAL_Screen_DrawStandbyImage();
        HAL_Screen_Update();
        enter_time = millis();
        is_sleeping = false;
    }

    void onLoop() override
    {
        // 【防呆检查】：如果设置为“永不休眠”，直接退出，不计时！
        if (sysConfig.true_sleep_time_ms == 0xFFFFFFFF)
            return;

        // 【核心休眠触发逻辑】：精准使用你原本的变量
        if (!is_sleeping && (millis() - enter_time > sysConfig.true_sleep_time_ms))
        {
            is_sleeping = true;

            // --- 1. UI 准备：休眠前清空屏幕，防止醒来闪烁旧画面 ---
            HAL_Screen_Clear();
            HAL_Screen_Update();

            // --- 2. 模块级休眠：App 管家下发指令 ---
            extern void SysHaptic_Sleep();
            SysHaptic_Sleep();
            extern void SysAudio_Sleep();
            SysAudio_Sleep();
            extern void SysNfc_Sleep();
            SysNfc_Sleep();

            // --- 3. 硬件级休眠：调用最新拆分的 HAL 底层 ---
            HAL_Sleep_Enter_Prepare(); // 引脚锁定，驱动 IC 挂起
            HAL_Sleep_Start();         // 触发真实的 esp_light_sleep_start

            // ==========================================
            // CPU 停转，直到用户物理按压旋钮唤醒
            // ==========================================

            // --- 4. 硬件级唤醒：防白屏与防按键误触护盾 ---
            HAL_Sleep_Wakeup_Post();

            // --- 5. 模块级唤醒 ---
            extern void SysHaptic_Wakeup();
            SysHaptic_Wakeup();
            extern void SysAudio_Wakeup();
            SysAudio_Wakeup();
            extern void SysNfc_Wakeup();
            SysNfc_Wakeup();

            // --- 6. 业务逻辑恢复 ---
            // 重新绘制待机画面，重置计时器，确保醒来后依然停留在待机页面
            HAL_Sleep_Wakeup_Post();
            HAL_Screen_Update();

            enter_time = millis();
            is_sleeping = false;

            appManager.resetIdleTimer();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        // 因为底层的 HAL_Sleep_Wakeup_Post 已经吞掉了“唤醒那一下”的按键
        // 所以当代码走到这里，说明是用户真正在亮屏待机状态下，短按了旋钮
        sysAudio.playTone(2000, 40);
        appManager.launchApp(appMainMenu);
    }
    // 【新增】：如果你想手动“点一下”就进待机（休眠），可以加长按逻辑

};

AppStandby instanceStandby;
AppBase *appStandby = &instanceStandby;