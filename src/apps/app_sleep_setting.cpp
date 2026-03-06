// 文件：src/apps/app_sleep_setting.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h" 

class AppSleepSetting : public AppBase {
private:
    int sleep_opt_idx;

    void drawUI() {
        HAL_Screen_Clear();

        const char* title = appManager.getLanguage() == LANG_ZH ? "休眠时间设定" : "SLEEP SETTINGS";
        drawAppWindow(title);

        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        int start_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2 - 13;

        HAL_Draw_Rect(20, start_y - 4, sw - 40, 26, 1);
        HAL_Fill_Triangle(30, start_y, 30, start_y + 14, 37, start_y + 7, 1);

        // 【极度舒适】：将内容彻底拆分为 变量 和 静态后缀
        const char* opts_zh[] = {"30", "60", "5", "永不"};
        const char* suff_zh[] = {" 秒 (推荐)", " 秒", " 分钟", "休眠"};
        
        const char* opts_en[] = {"30", "60", "5", "NEVER "};
        const char* suff_en[] = {" SECONDS", " SECONDS", " MINUTES", "SLEEP"};
        
        const char* val = (appManager.getLanguage() == LANG_ZH) ? opts_zh[sleep_opt_idx] : opts_en[sleep_opt_idx];
        const char* suff = (appManager.getLanguage() == LANG_ZH) ? suff_zh[sleep_opt_idx] : suff_en[sleep_opt_idx];
        
        // 传递前缀（空）、变量（跳动）、后缀（静止不动）
        drawSegmentedAnimatedText(45, start_y - 2, "", val, suff, 0.0f);
        
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        sleep_opt_idx = 0; 
        drawUI();
    }

    void onLoop() override {
        // 由于底层的锁帧保护，现在 drawUI 最多只会以 60FPS 渲染，频闪彻底消除！
        if (updateEditAnimation()) {
            drawUI();
        }
    }
    
    void onDestroy() override {}

    void onKnob(int delta) override {
        sleep_opt_idx = (sleep_opt_idx + delta + 4) % 4;
        triggerEditAnimation(delta);
        SYS_SOUND_GLITCH();
        drawUI(); 
    }

    void onKeyShort() override {
        SYS_SOUND_CONFIRM(); 
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