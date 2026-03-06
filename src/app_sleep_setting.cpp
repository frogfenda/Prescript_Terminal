// 文件：src/app_sleep_setting.cpp
#include "app_base.h"
#include "app_manager.h"

class AppSleepSetting : public AppBase {
private:
    int sleep_opt_idx;

    // 【新增】专属的 UI 绘制私有方法
    void drawUI() {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        HAL_Screen_ShowChineseLine(70, 16, appManager.getLanguage() == LANG_ZH ? "休眠时间设定" : "SLEEP SETTINGS");
        HAL_Draw_Line(20, 38, 220, 38, 1);

        int start_y = 90;
        HAL_Draw_Rect(20, start_y - 4, 200, 26, 1);
        HAL_Fill_Triangle(30, start_y, 30, start_y + 14, 37, start_y + 7, 1);

        const char* opts_zh[] = {"30 秒 (推荐)", "60 秒", "5 分钟", "永不休眠"};
        const char* opts_en[] = {"30 SECONDS", "60 SECONDS", "5 MINUTES", "NEVER SLEEP"};
        HAL_Screen_ShowChineseLine(50, start_y - 2, (appManager.getLanguage() == LANG_ZH) ? opts_zh[sleep_opt_idx] : opts_en[sleep_opt_idx]);
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
        appManager.popApp(); // 【修改】：保存后原路返回
    }
    void onKeyLong() override { appManager.popApp(); } // 【修改】：长按取消也原路返回
};

AppSleepSetting instanceSleepSetting;
AppBase* appSleepSetting = &instanceSleepSetting;