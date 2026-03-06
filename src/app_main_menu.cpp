// 文件：src/app_main_menu.cpp
#include "app_menu_base.h"

class AppMainMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 3; }
    
    const char* getTitle() override {
        // 【修复】：使用 appManager.getLanguage()
        return (appManager.getLanguage() == LANG_ZH) ? "系统主控制台" : "MAIN CONSOLE";
    }
    
    const char* getItemText(int index) override {
        // 【修复】：使用 appManager.getLanguage()
        if (appManager.getLanguage() == LANG_ZH) {
            const char* items[] = {"运行都市程序", "系统设置", "返回待机画面"};
            return items[index];
        } else {
            const char* items[] = {"EXECUTE PRESCRIPT", "SYSTEM SETTINGS", "RETURN STANDBY"};
            return items[index];
        }
    }
    
    void onItemClicked(int index) override {
        if (index == 0) appManager.launchApp(appPrescript);
        else if (index == 1) appManager.launchApp(appSystemSettings);
        else if (index == 2) appManager.launchApp(appStandby);
    }
    
    void onLongPressed() override {
        appManager.launchApp(appStandby);
    }
};

AppMainMenu instanceMainMenu;
AppBase* appMainMenu = &instanceMainMenu;