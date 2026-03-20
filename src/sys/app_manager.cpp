// 文件：src/sys/app_manager.cpp
#include "app_manager.h"
#include "sys_network.h"
#include "sys_config.h"
#include <Arduino.h>
#include "sys_ble.h"
#include "sys_fs.h"
#include "sys_res.h"
#include "sys_router.h"
#include "sys_event.h"
#include "sys_auto_push.h"
#include <queue>
#include <mutex>
std::queue<String> g_ble_msg_queue; // 无限容量的动态队列
std::mutex g_ble_mutex;             // 线程安全锁

volatile bool g_cross_core_trigger_push = false;

void _Cb_SysNotify(void *payload)
{
    Evt_Notify_t *p = (Evt_Notify_t *)payload;
    PushNotify_Trigger_Custom(p->text, p->keep_stack);
}

AppManager appManager;

extern void SysBLE_Notify(const char *data);

AppManager::AppManager()
{
    currentApp = nullptr;
    stackTop = 0;
    btn_press_start_time = 0;
    btn_is_holding = false;
    long_press_handled = false;
    current_lang = LANG_ZH;
    config_sleep_time_ms = 30000;
}

void AppManager::registerBackgroundApp(AppBase *app)
{
    if (bg_app_count < 10 && app != nullptr)
    {
        bg_apps[bg_app_count++] = app;
    }
}

void AppManager::installApp(AppBase* app) {
    if (app != nullptr) {
        app->onSystemInit(); // 触发 App 的自我安装程序！
    }
}

void AppManager::begin()
{
    current_lang = (SystemLang_t)sysConfig.language;
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    last_tick = millis();
    idle_timer = millis();

    SysRes_Init(); 

    
    // 系统级的特殊拦截保留在这里
    SysEvent_Subscribe(EVT_NOTIFY_CUSTOM, _Cb_SysNotify);
    
    // ==========================================
    // 【终极清爽的 App 挂载流水线】
    // ==========================================
    installApp(appSchedule);
    installApp(appAlarm);
    installApp(appPomodoro);
    installApp(appPrescriptList);
    // (未来如果有新的 App，直接往这里加一行 installApp 即可，其他什么都不用管！)
    
    launchApp(appStandby);
}

void AppManager::launchApp(AppBase *newApp)
{
    stackTop = 0;
    if (currentApp)
        currentApp->onDestroy();
    currentApp = newApp;
    currentApp->onCreate();
    currentApp->onResume(); // 【核心补丁】：强制触发界面的首次渲染，杜绝一切黑屏！
}

void AppManager::pushApp(AppBase *newApp)
{
    if (stackTop < MAX_NAV_STACK)
    {
        if (currentApp)
        {
            navStack[stackTop++] = currentApp;
            currentApp->onBackground();
        }
        currentApp = newApp;
        currentApp->onCreate();
        currentApp->onResume(); // 【核心补丁】：同上，保证推进栈的页面立刻显示！
    }
}

void AppManager::popApp()
{
    if (currentApp)
        currentApp->onDestroy();
    if (stackTop > 0)
    {
        currentApp = navStack[--stackTop];
        currentApp->onResume();
    }
    else
    {
        launchApp(appMainMenu);
    }
}

void AppManager::replaceApp(AppBase *newApp)
{
    if (currentApp)
    {
        currentApp->onDestroy();
    }
    currentApp = newApp;
    currentApp->onCreate();
    currentApp->onResume();
}

void AppManager::resetIdleTimer() { idle_timer = millis(); }

void AppManager::run()
{
   Network_Update();
    for (int i = 0; i < bg_app_count; i++)
    {
        bg_apps[i]->onBackgroundTick();
    }

    if (g_cross_core_trigger_push)
    {
        g_cross_core_trigger_push = false;
        PushNotify_Trigger_Random(true);
    }

    // ==========================================
    // 【核心修复】：在绝对安全的主线程 (Core 1) 拆快递！
    // ==========================================
    String pending_ble_msg = "";
    {
        // 锁住邮筒，拿出一封信就跑
        std::lock_guard<std::mutex> lock(g_ble_mutex);
        if (!g_ble_msg_queue.empty()) {
            pending_ble_msg = g_ble_msg_queue.front();
            g_ble_msg_queue.pop();
        }
    }
    // 把信交给路由器（此时写硬盘绝对不会和 UI 冲突！）
    if (pending_ble_msg.length() > 0) {
        SysRouter_ProcessBLE(pending_ble_msg);
    }
    // ==========================================
    // 【解耦中心】：调用极其干净的路由协议分发器
   
    uint32_t current_time = millis();
    last_tick = current_time;

    if (currentApp == nullptr)
        return;

    int knob_delta = HAL_Get_Knob_Delta();
    if (knob_delta != 0)
    {
        resetIdleTimer();
        currentApp->onKnob(knob_delta);
    }

    bool is_pressed = HAL_Is_Key_Pressed();
    if (is_pressed)
    {
        resetIdleTimer();
        if (!btn_is_holding)
        {
            btn_press_start_time = current_time;
            btn_is_holding = true;
            long_press_handled = false;
        }
        else if (!long_press_handled && (current_time - btn_press_start_time > BTN_LONG_PRESS_MS))
        {
            long_press_handled = true;
            SYS_SOUND_LONG();
            resetIdleTimer();
            currentApp->onKeyLong();
        }
    }
    else
    {
        if (btn_is_holding)
        {
            uint32_t duration = current_time - btn_press_start_time;
            btn_is_holding = false;
            if (!long_press_handled && duration > BTN_DEBOUNCE_MS)
            {
                resetIdleTimer();
                currentApp->onKeyShort();
            }
        }
    }

    currentApp->onLoop();

    if (currentApp != appStandby)
    {
        if (config_sleep_time_ms != 0xFFFFFFFF && (millis() - idle_timer > config_sleep_time_ms))
        {
            launchApp(appStandby);
        }
    }
}