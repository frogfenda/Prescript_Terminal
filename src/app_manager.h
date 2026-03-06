// 文件：src/app_manager.h
#ifndef __APP_MANAGER_H
#define __APP_MANAGER_H

#include "app_base.h"

// 【移交过来】：全局语言枚举
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

    // 【新增】：管家负责存储当前的系统语言
    SystemLang_t current_lang;

public:
    uint32_t config_sleep_time_ms; 

    AppManager();
    void launchApp(AppBase* newApp);
    void run(); 
    void resetIdleTimer();

    // 【新增】：暴露给外部的极其轻量的语言控制接口
    SystemLang_t getLanguage() { return current_lang; }
    void toggleLanguage() { current_lang = (current_lang == LANG_EN) ? LANG_ZH : LANG_EN; }
};

extern AppManager appManager; 

// 所有应用的指针声明
extern AppBase* appStandby;
extern AppBase* appMainMenu;
extern AppBase* appPrescript;
extern AppBase* appSleepSetting;
extern AppBase* appNetworkSync;
extern AppBase* appSystemSettings;

#endif