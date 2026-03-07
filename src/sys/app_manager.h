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

// 【新增】：番茄钟和闹钟全局指针
extern AppBase* appPomodoro;
extern AppBase* appSchedule; 
extern AppBase* appAlarm; 
// 【指令弹窗 API】
void Prescript_Launch_PushNormal(); 
void Prescript_Launch_PushDirect(); 
void Prescript_Launch_Custom(const char* custom_text); 
void Prescript_Launch_Custom_Wait(const char* custom_text); 
void PushNotify_Trigger_Random(bool keep_stack = false); 
void PushNotify_Trigger_Custom(const char* custom_text, bool keep_stack = false);
// 【闹钟后台任务与手机接口】
void Alarm_UpdateBackground();
void Alarm_AddPresetMobile(const char* name, int hour, int min, const char* text);
void Schedule_UpdateBackground();
void Schedule_AddMobile(uint32_t target_time, const char* title, const char* text);

extern volatile bool g_cross_core_trigger_push;

#endif
