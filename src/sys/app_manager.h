// 文件：src/app_manager.h (部分修改)
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

    AppBase* navStack[5];  
    int stackTop;          

public:
    uint32_t config_sleep_time_ms; 

    AppManager();
    
    // 【新增】：系统启动同步函数
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

#endif