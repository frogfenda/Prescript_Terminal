// 文件：src/app_manager.cpp (部分修改)
#include "app_manager.h"
#include "sys_config.h" // 【新增】：引入配置中心

AppManager appManager;

AppManager::AppManager() {
    currentApp = nullptr;
    btn_press_start_time = 0;
    btn_is_holding = false;
    long_press_handled = false;
    idle_timer = 0;
    last_tick = 0;
    stackTop = -1;
    // 构造函数里不再写死数据，交由 begin() 去处理
}

// 【新增】：开机同步配置并启动第一个 APP
void AppManager::begin() {
    // 1. 同步硬盘配置到系统管理器
    current_lang = (SystemLang_t)sysConfig.language;
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    
    // 2. 启动系统（跳转到待机界面）
    launchApp(appStandby); 
}

// ... 下面的 launchApp, pushApp, popApp, run 等保持你原来的代码不变即可 ...
// 绝对跳转：清空所有历史记忆
void AppManager::launchApp(AppBase* newApp) {
    stackTop = -1; 
    if (currentApp != nullptr) currentApp->onDestroy();
    currentApp = newApp;
    if (currentApp != nullptr) currentApp->onCreate();
    resetIdleTimer();
}

void AppManager::pushApp(AppBase* newApp) {
    if (currentApp != nullptr) {
        if (stackTop < 4) {
            stackTop++;
            navStack[stackTop] = currentApp; 
            currentApp->onBackground(); 
        } else {
            currentApp->onDestroy(); 
        }
    }
    currentApp = newApp;
    if (currentApp != nullptr) currentApp->onCreate();
    resetIdleTimer();
}

void AppManager::popApp() {
    if (stackTop >= 0) {
        AppBase* prevApp = navStack[stackTop]; 
        stackTop--;                            
        if (currentApp != nullptr) currentApp->onDestroy();
        currentApp = prevApp;
        if (currentApp != nullptr) currentApp->onResume(); 
        resetIdleTimer();
    } else {
        launchApp(appMainMenu);
    }
}

void AppManager::resetIdleTimer() { idle_timer = 0; }

void AppManager::run() {
    uint32_t current_time = millis();
    uint32_t delta_time = current_time - last_tick;
    last_tick = current_time;

    if (currentApp == nullptr) return;

    int knob_delta = HAL_Get_Knob_Delta();
    if (knob_delta != 0) {
        resetIdleTimer();
        currentApp->onKnob(knob_delta);
    }

    bool is_pressed = HAL_Is_Key_Pressed();
    if (is_pressed) {
        if (!btn_is_holding) {
            btn_press_start_time = current_time;
            btn_is_holding = true;
            long_press_handled = false;
        } else if (!long_press_handled && (current_time - btn_press_start_time > 600)) {
            long_press_handled = true;
            HAL_Buzzer_Play_Tone(800, 150);
            resetIdleTimer();
            currentApp->onKeyLong();
        }
    } else {
        if (btn_is_holding) {
            uint32_t duration = current_time - btn_press_start_time;
            btn_is_holding = false;
            if (!long_press_handled && duration > 50) {
                resetIdleTimer();
                currentApp->onKeyShort();
            }
        }
    }

    currentApp->onLoop();

    if (currentApp != appStandby) {
        idle_timer += delta_time;
        if (config_sleep_time_ms != 0xFFFFFFFF && idle_timer > config_sleep_time_ms) {
            launchApp(appStandby);
        }
    }
}