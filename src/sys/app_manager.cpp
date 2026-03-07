// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_config.h" 
#include "sys_network.h" 

AppManager appManager;
volatile bool g_cross_core_trigger_push = false;

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
        if (stackTop < MAX_NAV_STACK - 1) { // 使用宏替换魔法数字 4
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
    Network_Update();

    // 【核心补齐】：每帧巡视所有闹钟，确保时间到了能触发！
    Alarm_UpdateBackground(); 

    // ==========================================
    // 【双核协同】：Core 1 的大管家每帧检查一次跨核信箱！
    // ==========================================
    if (g_cross_core_trigger_push) {
        g_cross_core_trigger_push = false; // 取出信件，清空信箱
        
        // 防呆保护：如果当前已经在破译指令了，就别再弹出了
        if (currentApp != appPrescript && currentApp != appPushNotify) {
            launchApp(appPushNotify); // 绝对接管屏幕！拉起都市警报！
        }
    }

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
        } else if (!long_press_handled && (current_time - btn_press_start_time > BTN_LONG_PRESS_MS)) {
            long_press_handled = true;
            SYS_SOUND_LONG(); // 使用语义化音效
            resetIdleTimer();
            currentApp->onKeyLong();
        }
    } else {
        if (btn_is_holding) {
            uint32_t duration = current_time - btn_press_start_time;
            btn_is_holding = false;
            if (!long_press_handled && duration > BTN_DEBOUNCE_MS) {
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