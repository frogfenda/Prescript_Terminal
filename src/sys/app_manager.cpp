// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_network.h"
#include <Arduino.h>


// 确保文件开头有这些：
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

void AppManager::begin() { last_tick = millis(); idle_timer = millis(); }

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

// 【修复】：记录最后一次操作的绝对时间，而不是清零
// 【注意】：务必确保 resetIdleTimer 也改成了记录当前的绝对时间戳 millis()
void AppManager::resetIdleTimer() { 
    idle_timer = millis(); 
}

void AppManager::run() {
    // ==========================================
    // 1. 系统底层服务与定时任务更新
    // ==========================================
    Network_Update();
    Alarm_UpdateBackground(); 
    Schedule_UpdateBackground();

    // ==========================================
    // 2. 基础蓝牙突发指令处理 (带系统并发锁)
    // ==========================================
    if (g_cross_core_trigger_push) {
        g_cross_core_trigger_push = false; 
        if (!AppManagerLock::isSystemBusy(currentApp)) {
            PushNotify_Trigger_Random(true); // 保护原堆栈，退回原应用
        }
    }

    // ==========================================
    // 3. 高级蓝牙多维路由信箱 (带系统并发锁)
    // ==========================================
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

    // ==========================================
    // 4. 旋钮事件处理
    // ==========================================
    int knob_delta = HAL_Get_Knob_Delta();
    if (knob_delta != 0) {
        resetIdleTimer();
        currentApp->onKnob(knob_delta);
    }

    // ==========================================
    // 5. 按键事件处理与休眠防呆控制
    // ==========================================
    bool is_pressed = HAL_Is_Key_Pressed();
    if (is_pressed) {
        // 【终极防呆】：只要手指还按在旋钮上，就不断重置休眠时间，绝对不息屏！
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

    // ==========================================
    // 6. 执行当前 App 的循环逻辑
    // ==========================================
    currentApp->onLoop();

    // ==========================================
    // 7. 绝对时间休眠判定 (彻底免疫动画卡顿导致的时间跳变)
    // ==========================================
    if (currentApp != appStandby) {
        if (config_sleep_time_ms != 0xFFFFFFFF && (millis() - idle_timer > config_sleep_time_ms)) {
            launchApp(appStandby);
        }
    }
}