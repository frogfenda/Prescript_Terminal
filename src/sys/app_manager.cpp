// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_config.h" 
#include "sys_network.h" // 【新增】：在这里引入网络引擎！

AppManager appManager;

AppManager::AppManager() {
    currentApp = nullptr;
    btn_press_start_time = 0;
    btn_is_holding = false;
    long_press_handled = false;
    idle_timer = 0;
    last_tick = 0;
    stackTop = -1;
}

void AppManager::begin() {
    current_lang = (SystemLang_t)sysConfig.language;
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    
    // 【架构转移 1】：管家启动时，顺手把网络后台任务挂上！
    Network_Connect(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());
    
    launchApp(appStandby); 
}

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
    // 【架构转移 2】：在管家的主循环里驱动网络底层的非阻塞服务！
    // 这样它就成了一个真正的系统级后台 Daemon 进程
    Network_Update();

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