// 文件：src/apps/app_system_settings.cpp
#include "app_menu_base.h"
#include "sys_config.h" 
#include "sys_network.h" // 引入网络状态去判断菜单名字

class AppSystemSettings : public AppMenuBase {
protected:
    // 现在系统设置有 5 个选项了
    int getMenuCount() override { return 5; } 
    
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }
    
    const char* getItemText(int index) override {
        // 动态判断当前是不是连网状态
        NetworkState state = Network_GetState();
        bool is_connected = (state == NET_CONNECTED || state == NET_SYNC_SUCCESS || state == NET_SYNCING_NTP);

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) return is_connected ? "断开无线网络" : "连接无线网络"; // 动态替换文本
            const char* items[] = {"", "同步网络时间", "切换系统语言", "设定休眠时间", "返回上一级"};
            return items[index];
        } else {
            if (index == 0) return is_connected ? "DISCONNECT WIFI" : "CONNECT WIFI"; // 动态替换文本
            const char* items[] = {"", "SYNC NTP TIME", "SWITCH LANGUAGE", "SLEEP SETTINGS", "BACK TO MAIN"};
            return items[index];
        }
    }
    
    void onItemClicked(int index) override {
        if (index == 0) appManager.pushApp(appWifiConnect);   // 0.进WiFi开关模块
        else if (index == 1) appManager.pushApp(appNetworkSync); // 1.进纯时间同步模块
        else if (index == 2) {
            appManager.toggleLanguage();
            sysConfig.language = (uint8_t)appManager.getLanguage();
            sysConfig.save();
            drawMenuUI(visual_selection); 
        }
        else if (index == 3) appManager.pushApp(appSleepSetting); 
        else if (index == 4) appManager.popApp();             
    }
    
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;