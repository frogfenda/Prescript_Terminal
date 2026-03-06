// 文件：src/app_manager.h
#ifndef __APP_MANAGER_H
#define __APP_MANAGER_H

#include "app_base.h"

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

    // 【新增】：历史导航栈 (最大支持 5 层深度)
    AppBase* navStack[5];  
    int stackTop;          

public:
    uint32_t config_sleep_time_ms; 

    AppManager();
    void launchApp(AppBase* newApp); // 绝对跳转（清空历史）
    void pushApp(AppBase* newApp);   // 压栈前进（记住上一页）
    void popApp();                   // 弹栈后退（返回上一页）

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

#endif