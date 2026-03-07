// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_network.h"
#include "sys_config.h" 
#include <Arduino.h>

volatile bool g_cross_core_trigger_push = false;
volatile bool g_ble_has_msg = false;
char g_ble_msg_buf[512] = {0};
AppManager appManager;

AppManager::AppManager() {
    currentApp = nullptr;
    stackTop = 0;
    btn_press_start_time = 0;
    btn_is_holding = false;
    long_press_handled = false;
    current_lang = LANG_ZH;
    config_sleep_time_ms = 30000; 
}

void AppManager::begin() { 
    // 【核心修复 1】：同步硬盘里的配置
    current_lang = (SystemLang_t)sysConfig.language;
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    last_tick = millis(); 
    idle_timer = millis(); 
    
    // 【核心修复 2】：主引擎点火起步！没有这句就是黑屏植物人！
    launchApp(appStandby); 
}

void AppManager::launchApp(AppBase* newApp) {
    stackTop = 0; 
    if (currentApp) currentApp->onDestroy();
    currentApp = newApp;
    currentApp->onCreate();
}

void AppManager::pushApp(AppBase* newApp) {
    if (stackTop < MAX_NAV_STACK) {
        if (currentApp) {
            navStack[stackTop++] = currentApp;
            currentApp->onBackground();
        }
        currentApp = newApp;
        currentApp->onCreate();
    }
}

void AppManager::popApp() {
    if (currentApp) currentApp->onDestroy();
    if (stackTop > 0) {
        currentApp = navStack[--stackTop];
        currentApp->onResume();
    } else {
        launchApp(appMainMenu);
    }
}

void AppManager::resetIdleTimer() { idle_timer = millis(); }

void AppManager::run() {
    Network_Update();
    Alarm_UpdateBackground(); 
    Schedule_UpdateBackground();

    if (g_cross_core_trigger_push) {
        g_cross_core_trigger_push = false; 
        if (!AppManagerLock::isSystemBusy(currentApp)) {
            PushNotify_Trigger_Random(true); 
        }
    }

    if (g_ble_has_msg) {
        g_ble_has_msg = false;
        String msg = g_ble_msg_buf;
        
        if (msg.startsWith("TXT:")) {
            if (!AppManagerLock::isSystemBusy(currentApp)) {
                PushNotify_Trigger_Custom(msg.substring(4).c_str(), true);
            }
        } 
        else if (msg.startsWith("ALM:")) {
            int p1 = msg.indexOf(':', 4);
            int p2 = msg.indexOf(':', p1+1);
            int p3 = msg.indexOf(':', p2+1);
            if (p1>0 && p2>0 && p3>0) {
                Alarm_AddPresetMobile(
                    msg.substring(p2+1, p3).c_str(),
                    msg.substring(4, p1).toInt(),
                    msg.substring(p1+1, p2).toInt(),
                    msg.substring(p3+1).c_str()
                );
            }
        }
        else if (msg.startsWith("POM:")) {
            int p1 = msg.indexOf(':', 4);
            int p2 = msg.indexOf(':', p1+1);
            int p3 = msg.indexOf(':', p2+1);
            if (p1>0 && p2>0 && p3>0) {
                extern void Pomodoro_UpdatePreset(int, const char*, int, int);
                Pomodoro_UpdatePreset(
                    msg.substring(4, p1).toInt(),
                    msg.substring(p1+1, p2).c_str(),
                    msg.substring(p2+1, p3).toInt(),
                    msg.substring(p3+1).toInt()
                );
            }
        }
    }

    uint32_t current_time = millis();
    last_tick = current_time;

    if (currentApp == nullptr) return;

    int knob_delta = HAL_Get_Knob_Delta();
    if (knob_delta != 0) {
        resetIdleTimer();
        currentApp->onKnob(knob_delta);
    }

    bool is_pressed = HAL_Is_Key_Pressed();
    if (is_pressed) {
        resetIdleTimer(); 
        if (!btn_is_holding) {
            btn_press_start_time = current_time;
            btn_is_holding = true;
            long_press_handled = false;
        } else if (!long_press_handled && (current_time - btn_press_start_time > BTN_LONG_PRESS_MS)) {
            long_press_handled = true;
            SYS_SOUND_LONG(); 
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
        if (config_sleep_time_ms != 0xFFFFFFFF && (millis() - idle_timer > config_sleep_time_ms)) {
            launchApp(appStandby);
        }
    }
}