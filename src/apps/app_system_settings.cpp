// 文件：src/apps/app_system_settings.cpp
#include "app_menu_base.h"
#include "sys_config.h" 
#include "sys_network.h" 

extern AppBase* appAnimSetting; 
extern AppBase* appVolumeSetting; // 【新增】：引入独立的音量设置应用

class AppSystemSettings : public AppMenuBase {
protected:
    int getMenuCount() override { return 7; } // 增加到 7 个
    
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }
    
    const char* getItemText(int index) override {
        NetworkState state = Network_GetState();
        bool is_connected = (state == NET_SYNCING_NTP || state == NET_SYNC_SUCCESS || state == NET_SYNCING_NTP);

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) return is_connected ? "断开无线网络" : "连接无线网络"; 
            const char* items[] = {"", "同步网络时间", "切换系统语言", "设定休眠时间", "系统音量调节", "解码动画配置", "返回上一级"};
            return items[index];
        } else {
            if (index == 0) return is_connected ? "DISCONNECT WIFI" : "CONNECT WIFI"; 
            const char* items[] = {"", "SYNC NTP TIME", "SWITCH LANGUAGE", "SLEEP SETTINGS", "MASTER VOLUME", "ANIMATION SETUP", "BACK TO MAIN"};
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
        else if (index == 4) appManager.pushApp(appVolumeSetting); // 【修改】：直接调用独立的外部应用实例
        else if (index == 5) appManager.pushApp(appAnimSetting); 
        else if (index == 6) appManager.popApp();             
    }
    
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;