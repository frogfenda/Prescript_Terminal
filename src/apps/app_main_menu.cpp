#include "app_menu_base.h"

class AppMainMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 5; } // 【修改】：加到 5 个
    
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "都市主控菜单" : "MAIN MENU";
    }
    
    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            // 【新增】：番茄专注协议
            const char* items[] = {"接受都市意志", "番茄专注协议", "指令推送配置", "系统高级设置", "进入待机模式"};
            return items[index];
        } else {
            const char* items[] = {"RECEIVE PRESCRIPT", "POMODORO TIMER", "PUSH SETTINGS", "SYSTEM SETTINGS", "STANDBY MODE"};
            return items[index];
        }
    }
   void onItemClicked(int index) override {
        if (index == 0) {
            // 【回归纯净】：主菜单主动进入，不需要宣告任何状态，默认享受无限抽卡和长按退出！
            appManager.pushApp(appPrescript);
        }
        else if (index == 1) appManager.pushApp(appPomodoro);       // 【新增入口】
        else if (index == 2) appManager.pushApp(appPushSetting);   
        else if (index == 3) appManager.pushApp(appSystemSettings); 
        else if (index == 4) appManager.launchApp(appStandby);
    }
    
    void onLongPressed() override { appManager.launchApp(appStandby); }

public:
    AppMainMenu() {}
};

AppMainMenu instanceMainMenu;
AppBase* appMainMenu = &instanceMainMenu;