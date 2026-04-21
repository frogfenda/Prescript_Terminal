// 文件：src/apps/app_system_settings.cpp
#include "app_menu_base.h"
#include "sys_config.h" 
#include "sys_network.h" 

extern AppBase* appAnimSetting; 
extern AppBase* appVolumeSetting; // 【新增】：引入独立的音量设置应用

class AppSystemSettings : public AppMenuBase {
protected:
    int getMenuCount() override { return 8; } // 增加到 7 个
    
    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }
    
   const char* getItemText(int index) override {
        NetworkState state = Network_GetState();

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) {
                // 【核心修复】：根据真实状态精准显示
                if (state == NET_SYNC_SUCCESS) return "断开无线网络";
                if (state == NET_CONNECTING || state == NET_SYNCING_NTP || state == NET_FETCHING_API) return "网络运行中...";
                return "连接无线网络"; // 断开或失败时显示这个
            }
            const char* items[] = {"", "同步网络时间", "提取部统计", "切换系统语言", "设定休眠时间", "音量与振动", "解码动画配置", "返回上一级"};
            return items[index];
        } else {
            if (index == 0) {
                if (state == NET_SYNC_SUCCESS) return "DISCONNECT WIFI";
                if (state == NET_CONNECTING || state == NET_SYNCING_NTP || state == NET_FETCHING_API) return "WIFI BUSY...";
                return "CONNECT WIFI";
            }
            const char* items[] = {"", "SYNC NTP TIME", "GACHA STATS", "SWITCH LANGUAGE", "SLEEP SETTINGS", "VOL&HAPTIC", "ANIMATION SETUP", "BACK TO MAIN"};
            return items[index];
        }
    }
    
void onItemClicked(int index) override {
        // 【修改 3】：重新调整跳转路由（所有的 index 顺延）
        if (index == 0) appManager.pushApp(appWifiConnect);   
        else if (index == 1) appManager.pushApp(appNetworkSync); 
        else if (index == 2) appManager.pushApp(appGachaStats); // <--- 【新增】：索引 2 跳转到统计面板
        else if (index == 3) {
            appManager.toggleLanguage();
            sysConfig.language = (uint8_t)appManager.getLanguage();
            sysConfig.save();
            drawMenuUI(visual_selection); 
        }
        else if (index == 4) appManager.pushApp(appSleepSetting); 
        else if (index == 5) appManager.pushApp(appVolumeSetting); 
        else if (index == 6) appManager.pushApp(appAnimSetting); 
        else if (index == 7) appManager.popApp();             
    }
    
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;