// 文件：src/apps/app_push_notify.cpp
#include "app_base.h"
#include "app_manager.h"
#include <stdio.h>
#include <string.h>
#include "sys_haptic.h"
#include "sys/sys_specials.h" // 引入特异点引擎
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys/sys_event.h"

bool g_push_notify_keep_stack = false;

extern AppBase *appPrescript;
extern AppBase *appStandby;

void PushNotify_Trigger_Random(bool keep_stack)
{
    // 【核心 1】：提前摇骰子，此时已经决定了是不是以实玛利！
    sysSpecials.rollRandom();

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
    // 【核心 2】：强行注入 NFC 或网络传来的自定义指令！
    sysSpecials.setCustom(text);

    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    // 压栈逻辑同上
    if (keep_stack)
    {
        if (appManager.getCurrentApp() == appPushNotify || appManager.getCurrentApp() == appPrescript)
        {
            appManager.replaceApp(appPushNotify);
        }
        else if (appManager.getCurrentApp() == appStandby)
        {
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

void PushNotify_Trigger_Special_Forced(bool keep_stack)
{
    // 注意：这里【不调用】 sysSpecials.rollRandom()
    // 而是直接利用 sysSpecials 缓冲区里已经通过 forceDrawByID 定好的数据

    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    // 压栈逻辑与之前完全一致
    if (keep_stack)
    {
        appManager.pushApp(appPushNotify);
    }
    else
    {
        appManager.launchApp(appPushNotify);
    }
}
// ==========================================
// 邮局回调：收到强制触发电报时的处理动作
// ==========================================
void _Cb_SpcForce(void *payload)
{
    Evt_SpcForce_t *p = (Evt_SpcForce_t *)payload;
    String id = String(p->id);

    // 1. 召唤命运枢纽，强行装载指定人物的第一条剧情（进度归 0 并设为 1）
    sysSpecials.forceDrawByID(id);

    // 2. 拉起警报弹窗（使用你在前两步里写好的“不摇号”弹窗接口）
    PushNotify_Trigger_Special_Forced(false);
}
class AppPushNotify : public AppBase
{
private:
    uint32_t blink_timer;
    bool show_text;

public:
    // 【新增】：利用系统初始化钩子自动摆摊收信
    void onSystemInit() override
    {
        SysEvent_Subscribe(EVT_SPECIAL_FORCE, _Cb_SpcForce);
    }

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
                // 【核心 3】：无脑索要命运标题，并用专属颜色渲染！
                DrawResult res = sysSpecials.getResult();
                int x = (HAL_Get_Screen_Width() - HAL_Get_Text_Width(res.title.c_str())) / 2;

                // 如果你的 HAL 没有 _Color 后缀，暂时用 HAL_Screen_ShowChineseLine 也可以
                HAL_Screen_ShowChineseLine_Color(x, HAL_Get_Screen_Height() / 2 - 8, res.title.c_str(), res.color);
            }
            HAL_Screen_Update();
        }
    }
    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();

        // 【核心 4】：告诉解码器“命运已定”，直接进入解码！
        extern void Prescript_Prepare_PreRolled();
        Prescript_Prepare_PreRolled();

        if (g_push_notify_keep_stack)
            appManager.replaceApp(appPrescript);
        else
            appManager.launchApp(appPrescript);
    }
    void onKeyLong() override { onKeyShort(); }
};

AppPushNotify instancePushNotify;
AppBase *appPushNotify = &instancePushNotify;