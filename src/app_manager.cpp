// 文件：src/app_manager.cpp
#include "app_manager.h"

AppManager appManager;

AppManager::AppManager() {
    currentApp = nullptr;
    btn_press_start_time = 0;
    btn_is_holding = false;
    long_press_handled = false;
    idle_timer = 0;
    last_tick = 0;
    config_sleep_time_ms = 30000; 
    current_lang = LANG_ZH;

    // 【新增】：初始化栈顶指针
    stackTop = -1;
}

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
            // 【核心修改】：不销毁，让老页面进入后台休眠
            currentApp->onBackground(); 
        } else {
            currentApp->onDestroy(); // 栈满了才销毁
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
        // 【核心修改】：原样唤醒老页面，保留它的所有内部变量！
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

    // 自动休眠（绝对跳转回待机）
    if (currentApp != appStandby) {
        idle_timer += delta_time;
        if (config_sleep_time_ms != 0xFFFFFFFF && idle_timer > config_sleep_time_ms) {
            launchApp(appStandby);
        }
    }
}