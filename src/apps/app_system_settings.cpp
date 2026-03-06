// 文件：src/apps/app_system_settings.cpp
#include "app_menu_base.h"
#include "sys_config.h" 
#include "sys_network.h" 

class AppSystemSettings : public AppMenuBase {
protected:
    int getMenuCount() override { return 6; } 
    
    // 【修复】：把刚才漏掉的获取标题函数加回来！
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }
    
    const char* getItemText(int index) override {
        NetworkState state = Network_GetState();
        bool is_connected = (state == NET_CONNECTED || state == NET_SYNC_SUCCESS || state == NET_SYNCING_NTP);

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) return is_connected ? "断开无线网络" : "连接无线网络"; 
            const char* items[] = {"", "同步网络时间", "切换系统语言", "设定休眠时间", "都市推送设置", "返回上一级"};
            return items[index];
        } else {
            if (index == 0) return is_connected ? "DISCONNECT WIFI" : "CONNECT WIFI"; 
            const char* items[] = {"", "SYNC NTP TIME", "SWITCH LANGUAGE", "SLEEP SETTINGS", "PUSH SETTINGS", "BACK TO MAIN"};
            return items[index];
        }
    }
    
    void onItemClicked(int index) override {
        if (index == 0) appManager.pushApp(appWifiConnect);   
        else if (index == 1) appManager.pushApp(appNetworkSync); 
        else if (index == 2) {
            appManager.toggleLanguage();
            sysConfig.language = (uint8_t)appManager.getLanguage();
            sysConfig.save();
            drawMenuUI(visual_selection); 
        }
        else if (index == 3) appManager.pushApp(appSleepSetting); 
        else if (index == 4) appManager.pushApp(appPushSetting); // 进推送设置
        else if (index == 5) appManager.popApp();             
    }
    
    // 【修复】：长按返回功能
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;