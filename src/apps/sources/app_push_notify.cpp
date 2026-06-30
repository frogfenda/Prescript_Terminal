// 文件：src/apps/app_push_notify.cpp
/*
【模块职责】指令到达弹窗。

触发来源包括自动推送、日程/闹钟、BLE/NFC 自定义通知和特殊指令强制触发。
本 App 不解码指令正文，只负责用全屏警报动画提示“有指令到达”，
用户短按后进入 app_prescript 继续执行乱码/解码动画。

大屏适配说明：
- 旧版本只在屏幕中央闪烁一行标题；
- 当前版本增加响应式边框、底部提示和居中标题区域；
- 闪烁节奏和音效/震动节奏保持不变，避免改变原有“指令突入”体验。
*/
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include <stdio.h>
#include <string.h>
#include "sys/sys_haptic.h"
#include "sys/sys_feedback.h"
#include "sys/sys_specials.h"
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys/sys_event.h"
#include "ui/ui_theme.h"

bool g_push_notify_keep_stack = false;


void PushNotify_Trigger_Random(bool keep_stack)
{
    // 先抽取结果，再打开弹窗。这样弹窗标题颜色和后续解码内容保持一致。
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

void PushNotify_Trigger_Custom(const char *text, bool keep_stack)
{
    // BLE/NFC/日程/闹钟传来的正文先塞入特殊指令缓冲区，再复用同一个弹窗和解码流程。
    sysSpecials.setCustom(text);

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

void PushNotify_Trigger_Special_Forced(bool keep_stack)
{
    // 特殊指令强制触发时，sysSpecials 内部已经装载好目标结果，这里只负责弹窗。
    g_push_notify_keep_stack = keep_stack;
    appManager.resetIdleTimer();

    if (keep_stack)
        appManager.push(AppId::PushNotify);
    else
        appManager.launch(AppId::PushNotify);
}

// 【事件回调】网页/蓝牙请求强制触发特殊指令时，先装载指定 ID，再打开警报弹窗。
void _Cb_SpcForce(void *payload)
{
    Evt_SpcForce_t *p = (Evt_SpcForce_t *)payload;
    if (!p)
        return;

    String id = String(p->id);
    sysSpecials.forceDrawByID(id);
    PushNotify_Trigger_Special_Forced(false);
}

class AppPushNotify : public AppBase
{
private:
    uint32_t blink_timer;
    bool show_text;

    void drawAlarmFrame(uint16_t color)
    {
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        int margin = max(12, sw / 34);
        int corner = UITheme::Frame::CornerSize() + 3;

        // 四角短线形成警报框，避免整屏大矩形过重，同时适合 428×142 长条比例。
        HAL_Draw_Line(margin, margin, margin + corner, margin, color);
        HAL_Draw_Line(margin, margin, margin, margin + corner, color);
        HAL_Draw_Line(sw - margin, margin, sw - margin - corner, margin, color);
        HAL_Draw_Line(sw - margin, margin, sw - margin, margin + corner, color);
        HAL_Draw_Line(margin, sh - margin, margin + corner, sh - margin, color);
        HAL_Draw_Line(margin, sh - margin, margin, sh - margin - corner, color);
        HAL_Draw_Line(sw - margin, sh - margin, sw - margin - corner, sh - margin, color);
        HAL_Draw_Line(sw - margin, sh - margin, sw - margin, sh - margin - corner, color);
    }

    void drawUI()
    {
        HAL_Sprite_Clear();

        DrawResult res = sysSpecials.getResult();
        uint16_t color = show_text ? res.color : TFT_DARKGREY;
        drawAlarmFrame(color);

        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // 闪烁时只隐藏标题正文，外框保留，形成“信号正在压入”的警报感。
        if (show_text)
        {
            int title_w = HAL_Get_Text_Width(res.title.c_str());
            int title_y = (sh - HAL_Get_Font_Line_Height(HAL_FONT_TITLE)) / 2 - 3;
            HAL_Screen_ShowLine_Font((sw - title_w) / 2, title_y, res.title.c_str(), HAL_FONT_TITLE, res.color);
        }

        

        HAL_Screen_Update();
    }

public:
    void onSystemInit() override
    {
        SysEvent_Subscribe(EVT_SPECIAL_FORCE, _Cb_SpcForce);
    }

    void onCreate() override
    {
        blink_timer = millis();
        show_text = true;

        /*
         * 先绘制警报画面，再播放进入音效。
         *
         * 这不是改变“指令无法拒绝”的交互设定：长按仍然复用短按接收指令。
         * 这里只调整进入弹窗的显示顺序，避免三段警报音阻塞期间屏幕仍停留在上一页，
         * 让用户先看到“指令已压入”的全屏警报，再听到警报音。
         */
        drawUI();

        // 弹窗进入音统一走 Feedback 中心，避免 PushNotify 直接散落音频/震动实现。
        Feedback_PlayAlertSequence();
    }

    void onLoop() override
    {
        if (millis() - blink_timer > 600)
        {
            blink_timer = millis();
            show_text = !show_text;

            if (show_text)
            {
                // 闪烁脉冲也统一走 Feedback 中心，音频和震动保持同一套警报语义。
                Feedback_PlayAlertPulse();
            }

            drawUI();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();

        // 告诉指令解码器使用已经抽好的结果，不再重新抽取。
        extern void Prescript_Prepare_PreRolled();
        Prescript_Prepare_PreRolled();

        if (g_push_notify_keep_stack)
            appManager.replace(AppId::Prescript);
        else
            appManager.launch(AppId::Prescript);
    }

    void onKeyLong() override { onKeyShort(); }
};

AppPushNotify instancePushNotify;
AppBase *appPushNotify = &instancePushNotify;
