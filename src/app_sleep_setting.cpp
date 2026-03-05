// 文件：src/app_sleep_setting.cpp
#include "app_base.h"
#include "app_manager.h"

class AppSleepSetting : public AppBase {
private:
    int sleep_opt_idx;

public:
    void onCreate() override {
        sleep_opt_idx = 0; // 默认停在第一项
        UI_DrawSleepSetting(sleep_opt_idx);
    }

    void onLoop() override {}

    void onDestroy() override {}

    void onKnob(int delta) override {
        // 旋钮切换选项，并在 0~3 之间循环
        sleep_opt_idx = (sleep_opt_idx + delta + 4) % 4;
        UI_DrawSleepSetting(sleep_opt_idx);
        HAL_Buzzer_Random_Glitch();
    }

    void onKeyShort() override {
        // 短按保存配置
        HAL_Buzzer_Play_Tone(2500, 80);
        switch (sleep_opt_idx) {
            case 0: appManager.config_sleep_time_ms = 30000; break;
            case 1: appManager.config_sleep_time_ms = 60000; break;
            case 2: appManager.config_sleep_time_ms = 300000; break;
            case 3: appManager.config_sleep_time_ms = 0xFFFFFFFF; break;
        }
        // 保存完毕，自动退回主菜单
        appManager.launchApp(appMainMenu);
    }

    void onKeyLong() override {
        // 长按：取消并退回主菜单
        appManager.launchApp(appMainMenu);
    }
};

AppSleepSetting instanceSleepSetting;
AppBase* appSleepSetting = &instanceSleepSetting;