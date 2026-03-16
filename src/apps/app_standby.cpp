// 文件：src/apps/app_standby.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h" // 必须引入配置系统！
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
        // 【防呆检查 1】：如果设置为“永不休眠”，直接退出，不计时！
        if (sysConfig.true_sleep_time_ms == 0xFFFFFFFF)
            return;

        // 【核心修复】：这里必须是 > sysConfig.true_sleep_time_ms，绝不能是 > 3000
        if (!is_sleeping && (millis() - enter_time > sysConfig.true_sleep_time_ms))
        {
            is_sleeping = true;

            HAL_Screen_Clear();
            HAL_Screen_Update();

            HAL_Sleep_Enter(); // 进入浅睡眠，物理黑屏

            // --- 等待物理按压旋钮唤醒 ---

            HAL_Sleep_Exit(); // 唤醒面板
            appManager.resetIdleTimer();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        sysAudio.playTone(2000, 40);
        appManager.launchApp(appMainMenu);
    }
    void onKeyLong() override {}
};

AppStandby instanceStandby;
AppBase *appStandby = &instanceStandby;