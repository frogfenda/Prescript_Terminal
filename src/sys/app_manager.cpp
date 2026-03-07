// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_network.h"
#include "sys_config.h" 
#include <Arduino.h>

volatile bool g_cross_core_trigger_push = false;
volatile bool g_ble_has_msg = false;
char g_ble_msg_buf[512] = {0};
AppManager appManager;

// =========================================================
// 【解耦中心】：独立的跨维协议路由器，将脏代码剥离出主调度器！
// =========================================================
static void BLE_Router_Process(String msg) {
    if (msg.startsWith("TXT:")) {
        if (!AppManagerLock::isSystemBusy(appManager.getCurrentApp())) {
            PushNotify_Trigger_Custom(msg.substring(4).c_str(), true);
        }
    } 
    else if (msg.startsWith("ALM:")) {
        // 安全切割: ALM:时:分:名:字
        int p1 = msg.indexOf(':', 4);
        int p2 = msg.indexOf(':', p1+1);
        int p3 = msg.indexOf(':', p2+1);
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            Alarm_AddPresetMobile(
                msg.substring(p2+1, p3).c_str(),
                msg.substring(4, p1).toInt(),
                msg.substring(p1+1, p2).toInt(),
                msg.substring(p3+1).c_str()
            );
        }
    }
    else if (msg.startsWith("POM:")) {
        // 安全切割: POM:索引:名:专注:休息
        int p1 = msg.indexOf(':', 4);
        int p2 = msg.indexOf(':', p1+1);
        int p3 = msg.indexOf(':', p2+1);
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            extern void Pomodoro_UpdatePreset(int, const char*, int, int);
            Pomodoro_UpdatePreset(
                msg.substring(4, p1).toInt(),
                msg.substring(p1+1, p2).c_str(),
                msg.substring(p2+1, p3).toInt(),
                msg.substring(p3+1).toInt()
            );
        }
    }
    else if (msg.startsWith("SCH:")) {
        // 【补齐遗漏接口】安全切割: SCH:月:日:时:分:标题:内容
        // 示例: SCH:11:15:09:30:系统维护:执行全面内存清洗
        int p1 = msg.indexOf(':', 4);
        int p2 = msg.indexOf(':', p1+1);
        int p3 = msg.indexOf(':', p2+1);
        int p4 = msg.indexOf(':', p3+1);
        int p5 = msg.indexOf(':', p4+1);
        if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0 && p5 > 0) {
            int mo = msg.substring(4, p1).toInt();
            int d  = msg.substring(p1+1, p2).toInt();
            int h  = msg.substring(p2+1, p3).toInt();
            int m  = msg.substring(p3+1, p4).toInt();
            String title = msg.substring(p4+1, p5);
            String text = msg.substring(p5+1);
            
            // 极其优雅的“时空推演”算法
            time_t now; time(&now);
            struct tm t_info; localtime_r(&now, &t_info);
            t_info.tm_mon = mo - 1; t_info.tm_mday = d; 
            t_info.tm_hour = h; t_info.tm_min = m; t_info.tm_sec = 0;
            time_t target = mktime(&t_info);
            if (target < now) {
                t_info.tm_year += 1; // 如果时间早于现在，自动推算到明年！
                target = mktime(&t_info);
            }
            Schedule_AddMobile(target, title.c_str(), text.c_str());
        }
    }
}

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
    current_lang = (SystemLang_t)sysConfig.language;
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    last_tick = millis(); 
    idle_timer = millis(); 
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

    // 【解耦中心】：调用极其干净的路由协议分发器
    if (g_ble_has_msg) {
        g_ble_has_msg = false;
        BLE_Router_Process(String(g_ble_msg_buf));
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