// 文件：src/app_system_settings.cpp
#include "app_menu_base.h"

class AppSystemSettings : public AppMenuBase {
protected:
    int getMenuCount() override { return 4; }
    
    const char* getTitle() override {
        // 【修复】：使用 appManager.getLanguage()
        return (appManager.getLanguage() == LANG_ZH) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }
    
    const char* getItemText(int index) override {
        // 【修复】：使用 appManager.getLanguage()
        if (appManager.getLanguage() == LANG_ZH) {
            const char* items[] = {"同步网络时间", "切换系统语言", "设定休眠时间", "返回上一级"};
            return items[index];
        } else {
            const char* items[] = {"NETWORK TIME SYNC", "SWITCH LANGUAGE", "SLEEP SETTINGS", "BACK TO MAIN"};
            return items[index];
        }
    }
    
    void onItemClicked(int index) override {
        if (index == 0) appManager.launchApp(appNetworkSync);
        else if (index == 1) {
            // 【修复】：使用管家的新函数切换语言
            appManager.toggleLanguage();
            drawMenuUI(visual_selection); 
        }
        else if (index == 2) appManager.launchApp(appSleepSetting);
        else if (index == 3) appManager.launchApp(appMainMenu);
    }
    
    void onLongPressed() override {
        appManager.launchApp(appMainMenu);
    }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;