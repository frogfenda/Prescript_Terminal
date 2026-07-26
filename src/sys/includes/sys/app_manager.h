/*
【模块职责】系统页面调度接口。对外提供 AppId 导航、后台 App 注册、语言状态、空闲计时和当前 App 查询；主循环中的硬件事件分发由 app_manager.cpp 实现。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/app_manager.h
#ifndef __APP_MANAGER_H
#define __APP_MANAGER_H

#include "sys/app_base.h"
#include "sys/sys_constants.h"
#include "sys/app_registry.h"
#include "lang/terminal_lang.h"

// ==========================================
// 交互与调度参数宏定义
// ==========================================
#define MAX_NAV_STACK PrescriptConst::MAX_NAV_STACK       // 最大支持的页面返回层级
#define BTN_LONG_PRESS_MS PrescriptConst::BUTTON_LONG_MS // 长按触发阈值(毫秒)
#define BTN_DEBOUNCE_MS PrescriptConst::BUTTON_DEBOUNCE_MS    // 短按防抖阈值(毫秒)

class AppManager
{
private:
    AppBase *currentApp;
    uint32_t btn_press_start_time;
    bool btn_is_holding;
    bool long_press_handled;
    uint32_t idle_timer;
    uint32_t last_tick;

    SystemLang_t current_lang;
    AppBase* bg_apps[PrescriptConst::MAX_BG_APPS]; 
    uint8_t bg_app_count = 0;
    AppBase *navStack[MAX_NAV_STACK];
    int stackTop;

public:
    uint32_t config_sleep_time_ms;

    AppManager();
    // 【接口说明】装载语言、资源和系统事件订阅，调用 AppRegistry 安装后台回调，最后进入 Standby 页面。
    void begin();

    // Preferred navigation API: AppId keeps cross-app dependencies centralized in AppRegistry.
    // 【接口说明】清空页面栈并启动指定 AppId，适合主菜单跳转和全局切换到待机/指令页。
    void launch(AppId id);
    void push(AppId id);
    // 【接口说明】销毁当前页面并用指定 AppId 替换，不保留当前页面返回关系。
    void replace(AppId id);
    void installApp(AppId id);

    // Low-level navigation API retained for rare local/private pages and compatibility.
    // 【接口说明】低层指针版启动接口；AppId 版本最终会调用它，保留给私有页面和兼容代码。
    void launchApp(AppBase *newApp);
    void pushApp(AppBase *newApp);
    // 【接口说明】关闭当前页面并恢复上一个页面；栈为空时回到主菜单。
    void popApp();
    void replaceApp(AppBase *newApp);
    
    // 【接口说明】主循环调度函数；处理后台 tick、跨核心推送、BLE 队列、旋钮/按键分发、当前 App onLoop 和空闲待机。
    void run();
    void resetIdleTimer();
    // 【接口说明】注册需要后台 tick 的 App，例如闹钟、日程、倒计时这类不在当前页面也要检查时间的模块。
    void registerBackgroundApp(AppBase* app);
    void installApp(AppBase* app);
    // 【接口说明】返回当前 UI 语言；锁定版由编译宏决定，运行时版由 sysConfig.language 决定。
    SystemLang_t getLanguage() const { return current_lang; }
    bool isLanguageLocked() const { return TerminalLang::LOCKED; }
    // 【接口说明】根据编译宏和sysConfig.language计算current_lang，必须在SysRes_Init前执行。
    void loadLanguageFromConfig();
    void toggleLanguage();
    AppBase *getCurrentApp() { return currentApp; }
    // 【接口说明】用 AppId 判断当前页面，替代比较全局 App 指针。
    bool isCurrent(AppId id);
};

extern AppManager appManager;

// 【接口说明】准备一次随机推送指令并进入 AppPrescript；PushNotify 确认后会调用这条路径。
void Prescript_Launch_PushNormal();
void Prescript_Launch_PushDirect();
// 【接口说明】把外部文本作为指令内容送入 AppPrescript，用于 TXT、日程、闹钟等自定义指令。
void Prescript_Launch_Custom(const char *custom_text);
void Prescript_Launch_Custom_Wait(const char *custom_text);

// 【接口说明】拉起随机推送弹窗；keep_stack 为 true 时保留当前页面返回关系。
void PushNotify_Trigger_Random(bool keep_stack = false);
void PushNotify_Trigger_Custom(const char *custom_text, bool keep_stack = false);
// 【接口说明】协议层删除闹钟入口；按名称删除配置中的闹钟并写业务 ACK。
void Alarm_DeleteMobile(const char *name);
void Alarm_AddPresetMobile(const char *name, int hour, int min, const char *text);
// 【接口说明】协议层添加日程入口；按时间戳、标题、文本写入普通/隐藏日程。
void Schedule_AddMobile(uint32_t target_time, const char *title, const char *text, bool is_hidden = false);
void Schedule_DeleteMobile(const char *title);
// BLE RX and cross-core push requests are routed through sys_ble_queue / sys_runtime_status.

#endif
