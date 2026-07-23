/*
【模块职责】App 调度核心。每帧先处理后台任务和 BLE 队列，再把旋钮/按键事件分派到当前 App；副按键双击全局进入指令页，主菜单长按开启 NFC 伪装，其余短按/长按作为旋钮主按键的平行输入。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/app_manager.cpp
#include "sys/app_manager.h"
#include "sys/sys_network.h"
#include "sys/sys_config.h"
#include <Arduino.h>
#include "sys/sys_ble.h"
#include "sys/sys_fs.h"
#include "sys/sys_res.h"
#include "sys/sys_oracle.h"
#include "sys/sys_router.h"
#include "sys/sys_event.h"
#include "sys/sys_auto_push.h"
#include "sys/sys_nfc.h"
#include "sys/sys_ble_queue.h"
#include "sys/sys_runtime_status.h"
#include "sys/sys_specials.h"
#include "sys/sys_reminder.h"
#include "sys/sys_gesture.h"

void _Cb_SysNotify(void *payload)
{
    Evt_Notify_t *p = (Evt_Notify_t *)payload;
    if (p)
        SysReminder_Submit(SysReminderKind::Custom, p->text, p->keep_stack);
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
    current_lang = TerminalLang::DEFAULT_LANG;
    config_sleep_time_ms = PrescriptConst::DEFAULT_IDLE_SLEEP_MS;
}

void AppManager::registerBackgroundApp(AppBase *app)
{
    if (bg_app_count < PrescriptConst::MAX_BG_APPS && app != nullptr)
    {
        bg_apps[bg_app_count++] = app;
    }
}

void AppManager::installApp(AppBase* app) {
    if (app != nullptr) {
        app->onSystemInit(); // 触发 App 的自我安装程序！
    }
}

void AppManager::loadLanguageFromConfig()
{
    if (TerminalLang::LOCKED)
    {
        current_lang = TerminalLang::DEFAULT_LANG;
        return;
    }
    current_lang = TerminalLang::Normalize(sysConfig.language);
}

void AppManager::toggleLanguage()
{
    if (TerminalLang::LOCKED)
        return;
    SystemLang_t next_lang = (current_lang == LANG_EN) ? LANG_ZH : LANG_EN;
    // 运行时测试版允许中英文切换。切换时先让 sysConfig 保存旧语言 profile、载入新语言 profile，
    // 这样使用者、闹钟、日程和特异点进度不会在中英文之间互相覆盖。
    sysConfig.loadLanguageProfile(next_lang);
    current_lang = next_lang;
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    /*
     * FATFS 内容按语言分目录。切换完成后在主循环同步重载身份池与特殊指令池，
     * 避免 UI 已切换语言，但提取部或特殊指令仍引用上一语言的 PSRAM 缓存。
     */
    (void)SysRes_LoadIdentityPool(current_lang);
    sysSpecials.begin();
}

void AppManager::begin()
{
    loadLanguageFromConfig();
    config_sleep_time_ms = sysConfig.sleep_time_ms;
    last_tick = millis();
    idle_timer = millis();

    SysRes_Init(); 
    // 纺织机答案池依赖资源管家挂载的 JSON 素材，必须在 SysRes_Init() 后解析。
    sysOracle.begin();

    
    // 系统级的特殊拦截保留在这里
    SysEvent_Subscribe(EVT_NOTIFY_CUSTOM, _Cb_SysNotify);
    
    // App 注册集中在 AppRegistry，AppManager 不再维护零散 install 清单。
    AppRegistry_InstallSystemApps();
    launch(AppId::Standby);
}

void AppManager::launch(AppId id)
{
    launchApp(AppRegistry_Get(id));
}

void AppManager::push(AppId id)
{
    pushApp(AppRegistry_Get(id));
}

void AppManager::replace(AppId id)
{
    replaceApp(AppRegistry_Get(id));
}

void AppManager::installApp(AppId id)
{
    installApp(AppRegistry_Get(id));
}

void AppManager::launchApp(AppBase *newApp)
{
    if (newApp == nullptr)
        return;
    stackTop = 0;
    if (currentApp)
        currentApp->onDestroy();
    currentApp = newApp;
    currentApp->onCreate();
    currentApp->onResume(); // 【核心补丁】：强制触发界面的首次渲染，杜绝一切黑屏！
}

void AppManager::pushApp(AppBase *newApp)
{
    if (newApp == nullptr)
        return;
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
        launch(AppId::MainMenu);
    }
}

void AppManager::replaceApp(AppBase *newApp)
{
    if (newApp == nullptr)
        return;
    if (currentApp)
    {
        currentApp->onDestroy();
    }
    currentApp = newApp;
    currentApp->onCreate();
    currentApp->onResume();
}

void AppManager::resetIdleTimer() { idle_timer = millis(); }

bool AppManager::isCurrent(AppId id)
{
    return currentApp == AppRegistry_Get(id);
}


void AppManager::run()
{

    for (int i = 0; i < bg_app_count; i++)
    {
        bg_apps[i]->onBackgroundTick();
    }

    if (SysRuntime_ConsumePushNotifyRequest())
    {
        SysReminder_Submit(SysReminderKind::Random, nullptr, true);
    }

    /* 所有后台/事件提醒先完成入队，再在本轮统一展示一条，避免互相覆盖。 */
    SysReminder_Update();

    // ==========================================
    // 【核心修复】：在绝对安全的主线程 (Core 1) 拆快递！
    // ==========================================
    String pending_ble_msg;
    if (SysBleQueue_Pop(pending_ble_msg)) {
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

    /*
     * 运动手势与实体旋钮在这里汇合：上下滚动复用所有页面已有的 onKnob()，
     * 其他语义事件再交给当前 App 的 onGesture()。每帧只分发一条，避免未来某个手势触发
     * 页面跳转后，同一帧剩余事件错误地落到新页面。
     */
    SysGestureEvent gesture = {};
    if (SysGesture_PopEvent(&gesture))
    {
        resetIdleTimer();
        if (gesture.type == SysGestureType::ScrollUp)
            currentApp->onKnob(1);
        else if (gesture.type == SysGestureType::ScrollDown)
            currentApp->onKnob(-1);
        else
            currentApp->onGesture(gesture);
    }

  // ==========================================
    // 1. 旋钮主按键分发 (删除了原有的面条代码，全面接入多态引擎)
    // ==========================================
    BtnEvent main_evt = HAL_Get_Btn_Main_Event();
    if (main_evt == BTN_DOUBLE) {
        resetIdleTimer();
        // 如果你需要旋钮双击有什么全局效果，写这里，否则下发给 APP
        currentApp->onKeyDouble();
    } else if (main_evt == BTN_LONG) {
        resetIdleTimer();
        SYS_SOUND_LONG();
        currentApp->onKeyLong();
    } else if (main_evt == BTN_SHORT) {
        resetIdleTimer();
        currentApp->onKeyShort();
    }

    // ==========================================
    // 2. 副按键 (Btn2) 分发
    // ==========================================
    BtnEvent b2_evt = HAL_Get_Btn2_Event();

    if (b2_evt == BTN_DOUBLE) {
        resetIdleTimer();
        SYS_SOUND_CONFIRM();
        if (!isCurrent(AppId::Prescript)) {
            launch(AppId::Prescript); // 全局双击拉起都市指令
        } else {
            currentApp->onBtn2Double(); 
        }
    } 
    else if (b2_evt == BTN_LONG) {
        resetIdleTimer();
        SYS_SOUND_LONG();
        if (isCurrent(AppId::MainMenu)) {
            // 【全局例外】卡伪装只允许在主菜单由侧键长按启动，避免抢占其他界面的长按操作。
            SysNfc_StartEmulation();
        } else {
            // 【平行输入】其他界面的侧键长按等价于旋钮主按键长按。
            currentApp->onKeyLong();
        }
    }
    else if (b2_evt == BTN_SHORT) {
        resetIdleTimer();

        if (SysNfc_IsEmulating() && !isCurrent(AppId::Prescript)) {
            // 【安全出口】卡伪装进行中时，侧键短按优先取消伪装；普通状态下再作为确认键使用。
            SysNfc_StopEmulation();         // 下发撤退指令
            Feedback_PlayAbort();    // 播放一声低频“滴”，确认打断
        } else {
            // 【平行输入】普通状态下，侧键短按等价于旋钮主按键短按。
            currentApp->onKeyShort();
        }
    }
    currentApp->onLoop(); // 继续执行 UI 刷新

    if (!isCurrent(AppId::Standby))
    {
        if (config_sleep_time_ms != PrescriptConst::NEVER_SLEEP_MS && (millis() - idle_timer > config_sleep_time_ms))
        {
            launch(AppId::Standby);
        }
    }
}
