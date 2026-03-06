// 文件：src/apps/app_sleep_setting.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h" 

class AppSleepSetting : public AppBase {
private:
    int sleep_opt_idx;

    void drawUI() {
        HAL_Screen_Clear();

        // 【极度解耦】：一句代码搞定整个顶部的状态栏！
        const char* title = appManager.getLanguage() == LANG_ZH ? "休眠时间设定" : "SLEEP SETTINGS";
        drawAppWindow(title);

        // ==========================================
        // 以下只保留应用专属的 UI 组件（菜单框和选项）
        // ==========================================
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // 依然基于 38 像素的安全线往下计算居中点
        int start_y = 38 + (sh - 38) / 2 - 13;

        HAL_Draw_Rect(20, start_y - 4, sw - 40, 26, 1);
        HAL_Fill_Triangle(30, start_y, 30, start_y + 14, 37, start_y + 7, 1);

        const char* opts_zh[] = {"30 秒 (推荐)", "60 秒", "5 分钟", "永不休眠"};
        const char* opts_en[] = {"30 SECONDS", "60 SECONDS", "5 MINUTES", "NEVER SLEEP"};
        const char* current_opt = (appManager.getLanguage() == LANG_ZH) ? opts_zh[sleep_opt_idx] : opts_en[sleep_opt_idx];
        
        HAL_Screen_ShowChineseLine(45, start_y - 2, current_opt);
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        sleep_opt_idx = 0; 
        drawUI();
    }

    void onLoop() override {}
    void onDestroy() override {}

    void onKnob(int delta) override {
        sleep_opt_idx = (sleep_opt_idx + delta + 4) % 4;
        drawUI();
        HAL_Buzzer_Random_Glitch();
    }

    void onKeyShort() override {
        HAL_Buzzer_Play_Tone(2500, 80);
        switch (sleep_opt_idx) {
            case 0: appManager.config_sleep_time_ms = 30000; break;
            case 1: appManager.config_sleep_time_ms = 60000; break;
            case 2: appManager.config_sleep_time_ms = 300000; break;
            case 3: appManager.config_sleep_time_ms = 0xFFFFFFFF; break;
        }
        
        sysConfig.sleep_time_ms = appManager.config_sleep_time_ms;
        sysConfig.save();
        
        appManager.popApp(); 
    }
    void onKeyLong() override { appManager.popApp(); } 
};

AppSleepSetting instanceSleepSetting;
AppBase* appSleepSetting = &instanceSleepSetting;