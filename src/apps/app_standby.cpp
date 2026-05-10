/*
【模块职责】待机页。显示 standby.bin，处理空闲后 Light Sleep，按键唤醒后恢复屏幕、音频、震动和 NFC。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_standby.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h"
#include "hal.h"
#include "sys_haptic.h"
#include "sys_audio.h"
#include "sys_nfc.h"

class AppStandby : public AppBase
{
private:
    uint32_t enter_time;
    bool is_sleeping;

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

    // 【函数说明】待机页循环：检测主按键进入主菜单，达到 true_sleep_time_ms 后执行 Light Sleep 并在唤醒后重绘待机图。
    void onLoop() override
    {
        // 【防呆检查】：如果设置为“永不休眠”，直接退出，不计时！
        if (sysConfig.true_sleep_time_ms == PrescriptConst::NEVER_SLEEP_MS)
            return;

        // 【核心休眠触发逻辑】：精准使用你原本的变量
        if (!is_sleeping && (millis() - enter_time > sysConfig.true_sleep_time_ms))
        {
            is_sleeping = true;

            // --- 1. UI 准备：休眠前清空屏幕，防止醒来闪烁旧画面 ---
            HAL_Screen_Clear();
            HAL_Screen_Update();

            // --- 2. 模块级休眠：App 管家下发指令 ---
            SysHaptic_Sleep();
            SysAudio_Sleep();
            SysNfc_Sleep();

            // --- 3. 硬件级休眠：调用最新拆分的 HAL 底层 ---
            HAL_Sleep_Enter_Prepare(); // 引脚锁定，驱动 IC 挂起
            HAL_Sleep_Start();         // 触发真实的 esp_light_sleep_start

            // ==========================================
            // CPU 停转，直到用户物理按压旋钮唤醒
            // ==========================================

            // --- 4. 唤醒：HAL_Sleep_Wakeup_Post() 统一恢复屏幕、背光、功放、震动、音频、NFC，并吞掉唤醒按键 ---
            HAL_Sleep_Wakeup_Post();

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

    // 【函数说明】主按键短按从待机页进入主菜单。
    void onKeyShort() override
    {
        // 因为底层的 HAL_Sleep_Wakeup_Post 已经吞掉了“唤醒那一下”的按键
        // 所以当代码走到这里，说明是用户真正在亮屏待机状态下，短按了旋钮
        Feedback_PlayWake();
        appManager.launch(AppId::MainMenu);
    }
    // 【新增】：如果你想手动“点一下”就进待机（休眠），可以加长按逻辑

};

AppStandby instanceStandby;
AppBase *appStandby = &instanceStandby;
