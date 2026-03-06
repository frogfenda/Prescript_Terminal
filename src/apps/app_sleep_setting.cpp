// 文件：src/app_sleep_setting.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h"

class AppSleepSetting : public AppBase {
private:
    int sleep_opt_idx;

    void drawUI() {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();

        // 【解耦 1】：动态获取尺寸
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // 【解耦 2】：标题自动居中
        const char* title = appManager.getLanguage() == LANG_ZH ? "休眠时间设定" : "SLEEP SETTINGS";
        int title_x = (sw - HAL_Get_Text_Width(title)) / 2;
        HAL_Screen_ShowChineseLine(title_x, 16, title);
        
        // 【解耦 3】：分割线自适应屏幕宽度 (左右各留白 20)
        HAL_Draw_Line(20, 38, sw - 20, 38, 1);

        // 【解耦 4】：计算有效区域的 Y 轴绝对居中点
        // 顶部占用了 38，所以中心点是 38 + (sh - 38)/2。再减去 13 (选择框高度 26 的一半)
        int start_y = 38 + (sh - 38) / 2 - 13;

        // 【解耦 5】：边框宽度自适应 (左右各留白 20)
        HAL_Draw_Rect(20, start_y - 4, sw - 40, 26, 1);
        HAL_Fill_Triangle(30, start_y, 30, start_y + 14, 37, start_y + 7, 1);

        const char* opts_zh[] = {"30 秒 (推荐)", "60 秒", "5 分钟", "永不休眠"};
        const char* opts_en[] = {"30 SECONDS", "60 SECONDS", "5 MINUTES", "NEVER SLEEP"};
        const char* current_opt = (appManager.getLanguage() == LANG_ZH) ? opts_zh[sleep_opt_idx] : opts_en[sleep_opt_idx];
        
        // 文字起始点固定在小三角形的右边
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