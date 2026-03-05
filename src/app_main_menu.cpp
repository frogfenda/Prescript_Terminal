// 文件：src/app_main_menu.cpp
#include "app_base.h"
#include "app_manager.h"

// 如果你有其他应用，这里引入它们的指针
// extern AppBase* appSleepSetting; 
// extern AppBase* appPrescriptExecute;

class AppMainMenu : public AppBase {
private:
    int current_selection;
    float visual_selection;
    const int menu_count = 5;

public:
    void onCreate() override {
        current_selection = 0;
        visual_selection = 0.0f;
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        UI_DrawMenu_Animated(visual_selection);
    }

    void onLoop() override {
        float target = (float)current_selection;
        float diff = target - visual_selection;
        if (abs(diff) > 0.01f) {
            if (abs(diff) > 1.5f) visual_selection = target;
            else visual_selection += diff * 0.25f; 
            UI_DrawMenu_Animated(visual_selection);
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override {
        current_selection = (current_selection + delta + menu_count) % menu_count;
        HAL_Buzzer_Random_Glitch();
    }

    void onKeyShort() override {
        HAL_Buzzer_Play_Tone(2500, 80);
        // === 路由中心：根据选中的菜单项启动对应的子应用 ===
        if (current_selection == 0) {
            appManager.launchApp(appPrescript);     // 运行都市程序
        } 
        else if (current_selection == 1) {
            appManager.launchApp(appNetworkSync);   // 同步网络时间
        }
        else if (current_selection == 2) {
            UI_Toggle_Language();                   // 切换语言 (轻量级，无需独立应用)
            UI_DrawMenu_Animated(visual_selection); 
        }
        else if (current_selection == 3) {
            appManager.launchApp(appSleepSetting);  // 设定休眠时间
        }
        else if (current_selection == 4) {
            appManager.launchApp(appStandby);       // 返回待机画面
        }
    }
    void onKeyLong() override {
        // 主菜单长按退回待机
        appManager.launchApp(appStandby);
    }
};

AppMainMenu instanceMainMenu;
AppBase* appMainMenu = &instanceMainMenu;