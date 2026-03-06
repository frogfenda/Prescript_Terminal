// 文件：src/app_system_settings.cpp
#include "app_menu_base.h"
#include "sys_config.h" // 【新增】：引入配置中心

class AppSystemSettings : public AppMenuBase {
protected:
    int getMenuCount() override { return 4; }
    
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }
    
    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            const char* items[] = {"同步网络时间", "切换系统语言", "设定休眠时间", "返回上一级"};
            return items[index];
        } else {
            const char* items[] = {"NETWORK TIME SYNC", "SWITCH LANGUAGE", "SLEEP SETTINGS", "BACK TO MAIN"};
            return items[index];
        }
    }
    
    void onItemClicked(int index) override {
        if (index == 0) appManager.pushApp(appNetworkSync);   
        else if (index == 1) {
            appManager.toggleLanguage();
            
            // 【核心修复】：保存到硬盘！
            sysConfig.language = (uint8_t)appManager.getLanguage();
            sysConfig.save();
            
            drawMenuUI(visual_selection); 
        }
        else if (index == 2) appManager.pushApp(appSleepSetting); 
        else if (index == 3) appManager.popApp();             
    }
    
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;