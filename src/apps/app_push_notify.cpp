#include "app_base.h"
#include "app_manager.h"
#include <stdio.h>
#include <string.h>
#include "sys_haptic.h"

int g_push_notify_mode = 0;
char g_push_notify_text[512] = {0};
bool g_push_notify_keep_stack = false;

extern AppBase *appPrescript;
extern AppBase *appStandby; // 【核心新增】：引入待机应用进行状态拦截！

// ==========================================
// 【核心魔法】：抢占式打断机制 (彻底修复休眠返回 Bug 版)
// ==========================================
void PushNotify_Trigger_Random(bool keep_stack)
{
    g_push_notify_mode = 0;
    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    if (keep_stack)
    {
        if (appManager.getCurrentApp() == appPushNotify || appManager.getCurrentApp() == appPrescript)
        {
            appManager.replaceApp(appPushNotify);
        }
        else if (appManager.getCurrentApp() == appStandby)
        {
            // 【核心修复】：如果是从息屏中被唤醒，绝对不要把“息屏状态”压入返回栈！
            // 强行清空栈，并修改 keep_stack 为 false，保证解码结束后直接回到主菜单！
            g_push_notify_keep_stack = false;
            appManager.launchApp(appPushNotify);
        }
        else
        {
            appManager.pushApp(appPushNotify);
        }
    }
    else
    {
        appManager.launchApp(appPushNotify);
    }
}

void PushNotify_Trigger_Custom(const char *text, bool keep_stack)
{
    g_push_notify_mode = 1;
    snprintf(g_push_notify_text, sizeof(g_push_notify_text), "%s", text);
    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    if (keep_stack)
    {
        if (appManager.getCurrentApp() == appPushNotify || appManager.getCurrentApp() == appPrescript)
        {
            appManager.replaceApp(appPushNotify);
        }
        else if (appManager.getCurrentApp() == appStandby)
        {
            // 【核心修复】：同上，打碎休眠梦境，强制开启新栈！
            g_push_notify_keep_stack = false;
            appManager.launchApp(appPushNotify);
        }
        else
        {
            appManager.pushApp(appPushNotify);
        }
    }
    else
    {
        appManager.launchApp(appPushNotify);
    }
}
class AppPushNotify : public AppBase
{
private:
    uint32_t blink_timer;
    bool show_text;

public:
    void onCreate() override
    {
        blink_timer = millis();
        show_text = true;
        sysAudio.playTone(1500, 200);
        delay(100);
        sysAudio.playTone(1500, 200);
        delay(100);
        sysAudio.playTone(2500, 600);
    }

    void onLoop() override
    {
        if (millis() - blink_timer > 600)
        {
            blink_timer = millis();
            show_text = !show_text;
            if (show_text)
            {
                SYS_HAPTIC_ALERT();
                sysAudio.playTone(2500, 50);
            }

            HAL_Sprite_Clear();
            if (show_text)
            {
                const char *txt;
                if (g_push_notify_mode == 0)
                    txt = (appManager.getLanguage() == LANG_ZH) ? "接受都市意志" : "RECEIVE PRESCRIPT";
                else
                    txt = (appManager.getLanguage() == LANG_ZH) ? " 接受都市意志 " : "RECEIVE PRESCRIPT";
                int x = (HAL_Get_Screen_Width() - HAL_Get_Text_Width(txt)) / 2;
                HAL_Screen_ShowChineseLine(x, HAL_Get_Screen_Height() / 2 - 8, txt);
            }
            HAL_Screen_Update();
        }
    }
    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        if (g_push_notify_mode == 0)
            Prescript_Launch_PushDirect();
        else
            Prescript_Launch_Custom(g_push_notify_text);

        if (g_push_notify_keep_stack)
        {
            // 【核心修复】：废除 pop + push，直接使用 replaceApp 平滑切入解码界面！
            appManager.replaceApp(appPrescript);
        }
        else
            appManager.launchApp(appPrescript);
    }
    void onKeyLong() override { onKeyShort(); }
};

AppPushNotify instancePushNotify;
AppBase *appPushNotify = &instancePushNotify;