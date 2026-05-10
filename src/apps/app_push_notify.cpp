/*
【模块职责】指令推送弹窗。接收随机推送、网页 TXT、日程/闹钟、特殊指令强制触发，闪烁提示后进入 AppPrescript。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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

// 【函数说明】生成一次随机推送弹窗：设置 keep_stack，使用普通推送标题，并通过 AppManager 切换到 PushNotify。
void PushNotify_Trigger_Random(bool keep_stack)
{
    // 【核心 1】：提前摇骰子，此时已经决定了是不是以实玛利！
    sysSpecials.rollRandom();

    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    if (keep_stack)
    {
        if (appManager.isCurrent(AppId::PushNotify) || appManager.isCurrent(AppId::Prescript))
        {
            appManager.replace(AppId::PushNotify);
        }
        else if (appManager.isCurrent(AppId::Standby))
        {
            g_push_notify_keep_stack = false;
            appManager.launch(AppId::PushNotify);
        }
        else
        {
            appManager.push(AppId::PushNotify);
        }
    }
    else
    {
        appManager.launch(AppId::PushNotify);
    }
}

// 【函数说明】生成自定义文本推送弹窗：保存外部文本，确认后进入 AppPrescript 显示该文本。
void PushNotify_Trigger_Custom(const char *text, bool keep_stack)
{
    // 【核心 2】：强行注入 NFC 或网络传来的自定义指令！
    sysSpecials.setCustom(text);

    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    // 压栈逻辑同上
    if (keep_stack)
    {
        if (appManager.isCurrent(AppId::PushNotify) || appManager.isCurrent(AppId::Prescript))
        {
            appManager.replace(AppId::PushNotify);
        }
        else if (appManager.isCurrent(AppId::Standby))
        {
            g_push_notify_keep_stack = false;
            appManager.launch(AppId::PushNotify);
        }
        else
        {
            appManager.push(AppId::PushNotify);
        }
    }
    else
    {
        appManager.launch(AppId::PushNotify);
    }
}

// 【函数说明】生成特殊指令强制触发弹窗：从 sysSpecials 取得标题和文本，确认后进入预抽指令流程。
void PushNotify_Trigger_Special_Forced(bool keep_stack)
{
    // 注意：这里【不调用】 sysSpecials.rollRandom()
    // 而是直接利用 sysSpecials 缓冲区里已经通过 forceDrawByID 定好的数据

    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    // 压栈逻辑与之前完全一致
    if (keep_stack)
    {
        appManager.push(AppId::PushNotify);
    }
    else
    {
        appManager.launch(AppId::PushNotify);
    }
}
// ==========================================
// 邮局回调：收到强制触发电报时的处理动作
// ==========================================
// 【函数说明】处理 SPC 命令：按特殊指令 ID 锁定对应文本并拉起强制触发弹窗。
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
    // 【函数说明】订阅 SPECIAL_FORCE 事件，让网页 SPC 命令可以直接触发特殊指令弹窗。
    void onSystemInit() override
    {
        SysEvent_Subscribe(EVT_SPECIAL_FORCE, _Cb_SpcForce);
    }

    // 【函数说明】弹窗出现时播放三段警报反馈，初始化闪烁计时器并绘制第一帧。
    void onCreate() override
    {
        blink_timer = millis();
        show_text = true;
        Feedback_PlayAlertSequence();
    }

    // 【函数说明】每 600ms 切换标题显示状态，形成警报闪烁，并同步播放警报脉冲。
    void onLoop() override
    {
        if (millis() - blink_timer > 600)
        {
            blink_timer = millis();
            show_text = !show_text;
            if (show_text)
            {
                Feedback_PlayAlertPulse();
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

    // 【函数说明】确认弹窗：随机推送进入随机抽取，自定义文本进入自定义指令，特殊指令进入预抽流程。
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();

        // 【核心 4】：告诉解码器“命运已定”，直接进入解码！
        extern void Prescript_Prepare_PreRolled();
        Prescript_Prepare_PreRolled();

        if (g_push_notify_keep_stack)
            appManager.replace(AppId::Prescript);
        else
            appManager.launch(AppId::Prescript);
    }
    // 【函数说明】长按与短按同义，允许用户用长按确认推送弹窗。
    void onKeyLong() override { onKeyShort(); }
};

AppPushNotify instancePushNotify;
AppBase *appPushNotify = &instancePushNotify;
