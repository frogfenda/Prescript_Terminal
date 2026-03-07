// 文件：src/sys/app_manager.h
#ifndef __APP_MANAGER_H
#define __APP_MANAGER_H

#include "app_base.h"

// ==========================================
// 交互与调度参数宏定义
// ==========================================
#define MAX_NAV_STACK      5    // 最大支持的页面返回层级
#define BTN_LONG_PRESS_MS  600  // 长按触发阈值(毫秒)
#define BTN_DEBOUNCE_MS    50   // 短按防抖阈值(毫秒)

typedef enum {
    LANG_EN = 0,
    LANG_ZH = 1
} SystemLang_t;

class AppManager {
private:
    AppBase* currentApp;
    uint32_t btn_press_start_time;
    bool btn_is_holding;
    bool long_press_handled;
    uint32_t idle_timer;
    uint32_t last_tick;

    SystemLang_t current_lang;

    AppBase* navStack[MAX_NAV_STACK];  
    int stackTop;          

public:
    uint32_t config_sleep_time_ms; 

    AppManager();
    void begin(); 
    void launchApp(AppBase* newApp); 
    void pushApp(AppBase* newApp);   
    void popApp();                   

    void run(); 
    void resetIdleTimer();

    SystemLang_t getLanguage() { return current_lang; }
    void toggleLanguage() { current_lang = (current_lang == LANG_EN) ? LANG_ZH : LANG_EN; }
    AppBase* getCurrentApp() { return currentApp; }

};

extern AppManager appManager; 
extern AppBase* appStandby;
extern AppBase* appMainMenu;
extern AppBase* appPrescript;
extern AppBase* appSleepSetting;
extern AppBase* appNetworkSync;
extern AppBase* appSystemSettings;
extern AppBase* appWifiConnect;

// 文件：src/sys/app_manager.h (仅替换文件最底部部分)

extern AppBase* appPushNotify;
extern AppBase* appPushSetting;
extern AppBase* appPomodoro;
extern AppBase* appAlarm; 
extern AppBase* appSchedule;

void Prescript_Launch_PushNormal(); 
void Prescript_Launch_PushDirect(); 
void Prescript_Launch_Custom(const char* custom_text); 
void Prescript_Launch_Custom_Wait(const char* custom_text); 

void PushNotify_Trigger_Random(bool keep_stack = false); 
void PushNotify_Trigger_Custom(const char* custom_text, bool keep_stack = false); 

void Alarm_UpdateBackground();
void Alarm_AddPresetMobile(const char* name, int hour, int min, const char* text);
void Schedule_UpdateBackground();
void Schedule_AddMobile(uint32_t target_time, const char* title, const char* text);

// ==========================================
// 【全新引擎】：并发锁与跨核信箱
// ==========================================
extern volatile bool g_cross_core_trigger_push;
extern volatile bool g_ble_has_msg;    // 蓝牙信箱标志
extern char g_ble_msg_buf[512];        // 蓝牙信件内容

class AppManagerLock {
public:
    static bool isSystemBusy(AppBase* current) {
        // 【逻辑重构】：如果是系统警报界面，绝对锁定
        if (current == appPushNotify) return true;
        
        // 【逻辑重构】：如果是在解密界面，且不是用户自己点着玩的(mode != 0)，也锁定
        extern int __internal_prescript_mode;
        if (current == appPrescript && __internal_prescript_mode != 0) return true;
        
        return false;
    }
};
#endif
