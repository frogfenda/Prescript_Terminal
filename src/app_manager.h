#ifndef __APP_MANAGER_H
#define __APP_MANAGER_H

#include "app_base.h"

class AppManager {
private:
    AppBase* currentApp;
    
    // 全局按键与休眠状态
    uint32_t btn_press_start_time;
    bool btn_is_holding;
    bool long_press_handled;
    uint32_t idle_timer;
    uint32_t last_tick;

public:
    uint32_t config_sleep_time_ms; // 全局休眠时间配置

    AppManager();
    void launchApp(AppBase* newApp);
    void run(); // 放在 loop() 里
    void resetIdleTimer();
};

extern AppManager appManager; // 全局单例

// 声明我们要用到的几个核心应用（稍后实现）
extern AppBase* appStandby;
extern AppBase* appMainMenu;
extern AppBase* appPrescript;
extern AppBase* appSleepSetting;
extern AppBase* appNetworkSync;

#endif