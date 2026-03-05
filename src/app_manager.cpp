#include "app_manager.h"

AppManager appManager; // 实例化大管家

AppManager::AppManager() {
    currentApp = nullptr;
    btn_press_start_time = 0;
    btn_is_holding = false;
    long_press_handled = false;
    idle_timer = 0;
    last_tick = 0;
    config_sleep_time_ms = 30000; // 默认30秒
}

void AppManager::launchApp(AppBase* newApp) {
    if (currentApp != nullptr) {
        currentApp->onDestroy();
    }
    currentApp = newApp;
    if (currentApp != nullptr) {
        currentApp->onCreate();
    }
    resetIdleTimer();
}

void AppManager::resetIdleTimer() {
    idle_timer = 0;
}

void AppManager::run() {
    uint32_t current_time = millis();
    uint32_t delta_time = current_time - last_tick;
    last_tick = current_time;

    if (currentApp == nullptr) return;

    // 1. 处理旋钮
    int knob_delta = HAL_Get_Knob_Delta();
    if (knob_delta != 0) {
        resetIdleTimer();
        currentApp->onKnob(knob_delta);
    }

    // 2. 处理按键 (短按与长按分离)
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
            currentApp->onKeyLong(); // 触发应用的长按事件
        }
    } else {
        if (btn_is_holding) {
            uint32_t duration = current_time - btn_press_start_time;
            btn_is_holding = false;
            if (!long_press_handled && duration > 50) {
                resetIdleTimer();
                currentApp->onKeyShort(); // 触发应用的短按事件
            }
        }
    }

    // 3. 运行当前应用的专属逻辑
    currentApp->onLoop();

    // 4. 全局自动休眠判定 (如果当前不是待机界面，且超时，则强制切回待机)
    if (currentApp != appStandby) {
        idle_timer += delta_time;
        if (config_sleep_time_ms != 0xFFFFFFFF && idle_timer > config_sleep_time_ms) {
            launchApp(appStandby); // 强制踢回待机界面
        }
    }
}