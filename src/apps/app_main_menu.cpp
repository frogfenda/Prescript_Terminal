// 文件：src/apps/app_main_menu.cpp
#include "app_menu_base.h"

class AppMainMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 4; } // 【修改】：从 3 个增加到 4 个
    
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "都市主控菜单" : "MAIN MENU";
    }
    
    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            // 【修改】：在第二位插入“指令推送配置”
            const char* items[] = {"接受都市意志", "指令推送配置", "系统设置", "返回待机界面"};
            return items[index];
        } else {
            const char* items[] = {"RECEIVE PRESCRIPT", "PUSH SETTINGS", "SYSTEM SETTINGS", "STANDBY MODE"};
            return items[index];
        }
    }
    
    void onItemClicked(int index) override {
        if (index == 0) {
            // 【规范调用】：声明要正常模式，然后推入 APP
            Prescript_SetMode_Normal();
            appManager.pushApp(appPrescript);
        }
        else if (index == 1) appManager.pushApp(appPushSetting);   
        else if (index == 2) appManager.pushApp(appSystemSettings); 
        else if (index == 3) appManager.launchApp(appStandby);
    }
    
    void onLongPressed() override { appManager.launchApp(appStandby); }

public:
    AppMainMenu() {}
};

AppMainMenu instanceMainMenu;
AppBase* appMainMenu = &instanceMainMenu;