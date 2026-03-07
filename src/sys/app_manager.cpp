// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_network.h"
#include "sys_config.h" 
#include <Arduino.h>
#include "sys_ble.h"
volatile bool g_cross_core_trigger_push = false;
volatile bool g_ble_has_msg = false;
char g_ble_msg_buf[512] = {0};
AppManager appManager;

// =========================================================
// 【解耦中心】：独立的跨维协议路由器，将脏代码剥离出主调度器！
// =========================================================
// 文件：src/sys/app_manager.cpp (找到并完全替换 BLE_Router_Process 函数)

// 文件：src/sys/app_manager.cpp (仅替换 BLE_Router_Process 函数)

extern void SysBLE_Notify(const char* data);

static void BLE_Router_Process(String msg) {
    if (msg.startsWith("GET:SYNC")) {
        SysBLE_Notify("SYNC:CLEAR"); 
        delay(50);
        
        for (int i = 0; i < sysConfig.alarm_count; i++) {
            // 【核心修复】：JSON 引号护盾，防止数据崩溃
            String safeName = sysConfig.alarms[i].name.c_str(); safeName.replace("\"", "\\\"");
            String safeTxt = sysConfig.alarms[i].prescript.c_str(); safeTxt.replace("\"", "\\\"");
            
            String out = "SYNC:ALM:{\"h\":" + String(sysConfig.alarms[i].hour) + 
                         ",\"m\":" + String(sysConfig.alarms[i].min) + 
                         ",\"n\":\"" + safeName + 
                         "\",\"t\":\"" + safeTxt + "\"}";
            SysBLE_Notify(out.c_str());
            delay(50); 
        }
        
        for (int i = 0; i < sysConfig.schedule_count; i++) {
            if (sysConfig.schedules[i].is_expired) continue;
            struct tm t_info; time_t tt = sysConfig.schedules[i].target_time; localtime_r(&tt, &t_info);
            char dt[32]; sprintf(dt, "%04d-%02d-%02dT%02d:%02d", t_info.tm_year+1900, t_info.tm_mon+1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min);
            
            // 【核心修复】：JSON 引号护盾
            String safeName = sysConfig.schedules[i].title.c_str(); safeName.replace("\"", "\\\"");
            String safeTxt = sysConfig.schedules[i].prescript.c_str(); safeTxt.replace("\"", "\\\"");
            
            String out = "SYNC:SCH:{\"dt\":\"" + String(dt) + 
                         "\",\"n\":\"" + safeName + 
                         "\",\"t\":\"" + safeTxt + "\"}";
            SysBLE_Notify(out.c_str());
            delay(50);
        }
        return; 
    }

    if (msg.startsWith("TXT:")) {
        PushNotify_Trigger_Custom(msg.substring(4).c_str(), true);
    }
    else if (msg.startsWith("ALM:")) {
        int p1 = msg.indexOf(':', 4); int p2 = msg.indexOf(':', p1+1); int p3 = msg.indexOf(':', p2+1);
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            Alarm_AddPresetMobile(msg.substring(p2+1, p3).c_str(), msg.substring(4, p1).toInt(), msg.substring(p1+1, p2).toInt(), msg.substring(p3+1).c_str());
        }
    }
    else if (msg.startsWith("ALM_DEL:")) {
        Alarm_DeleteMobile(msg.substring(8).c_str());
    }
    else if (msg.startsWith("POM:")) {
        int p1 = msg.indexOf(':', 4); int p2 = msg.indexOf(':', p1+1); int p3 = msg.indexOf(':', p2+1);
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            extern void Pomodoro_UpdatePreset(int, const char*, int, int);
            Pomodoro_UpdatePreset(msg.substring(4, p1).toInt(), msg.substring(p1+1, p2).c_str(), msg.substring(p2+1, p3).toInt(), msg.substring(p3+1).toInt());
        }
    }
    else if (msg.startsWith("SCH:")) {
        // 【核心修复】：补齐年份协议位，彻底解决时空错乱！
        int p1 = msg.indexOf(':', 4); int p2 = msg.indexOf(':', p1+1); int p3 = msg.indexOf(':', p2+1); int p4 = msg.indexOf(':', p3+1); int p5 = msg.indexOf(':', p4+1); int p6 = msg.indexOf(':', p5+1);
        if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0 && p5 > 0 && p6 > 0) {
            int y  = msg.substring(4, p1).toInt();
            int mo = msg.substring(p1+1, p2).toInt();
            int d  = msg.substring(p2+1, p3).toInt();
            int h  = msg.substring(p3+1, p4).toInt();
            int m  = msg.substring(p4+1, p5).toInt();
            String title = msg.substring(p5+1, p6);
            String text = msg.substring(p6+1);
            
            time_t now; time(&now);
            struct tm t_info; localtime_r(&now, &t_info);
            t_info.tm_year = y - 1900; 
            t_info.tm_mon = mo - 1; 
            t_info.tm_mday = d; 
            t_info.tm_hour = h; 
            t_info.tm_min = m; 
            t_info.tm_sec = 0;
            time_t target = mktime(&t_info);
            Schedule_AddMobile(target, title.c_str(), text.c_str());
        }
    }
    else if (msg.startsWith("SCH_DEL:")) {
        Schedule_DeleteMobile(msg.substring(8).c_str());
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
        // 直接无脑打断！
        PushNotify_Trigger_Random(true); 
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