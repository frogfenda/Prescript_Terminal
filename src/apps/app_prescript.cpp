/*
【模块职责】指令接收页。负责抽取普通/特殊指令、播放 procedure 循环音、调用 UIPrescript 四种解码动画，并在完成态支持滚动阅读。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_auto_push.h"
#include "sys_config.h"
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys/sys_res.h"
#include "sys_haptic.h"
#include "sys_specials.h"
#include "../ui/ui_prescript_decoder.h"

bool g_prescript_needs_roll = true;

// 【函数说明】特殊指令弹窗确认前调用：标记下一次进入 AppPrescript 时不要重新随机，而是使用 sysSpecials 已锁定的结果。
void Prescript_Prepare_PreRolled()
{
    g_prescript_needs_roll = false; // 被推送唤醒时调用，告诉自己不要重新摇号
}

namespace {
static const int ANIM_CHAOS_DELAY = 15;

// 【函数说明】解码动画播放期间的音频保活回调；确保 procedure.wav 循环声在长动画中持续存在。
void PrescriptProcedureTick()
{
    if (!g_wav_procedure)
        SYS_SOUND_GLITCH();
}
}

class AppPrescript : public AppBase
{
private:
    enum State
    {
        S_INIT,
        S_WAIT_RELEASE,
        S_CHAOS,
        S_DECODE,
        S_DONE
    };

    State m_state;
    UIPrescript::TextLayout m_layout;
    int m_scroll_offset = 0;

    void drawChaosFrame()
    {
        UIPrescript::DrawChaosFrame(appManager.getLanguage(), sysSpecials.getResult().color);
        SYS_SOUND_GLITCH();
    }

    void drawDoneFrame()
    {
        UIPrescript::DrawDoneFrame(m_layout, m_scroll_offset);
    }

    void startProcedureLoop()
    {
        if (g_wav_procedure)
            sysAudio.playWAV(g_wav_procedure, g_wav_procedure_len, true);
    }

    void playFinalFeedback(const DrawResult& res)
    {
        sysAudio.stopWAV();

        String bind = res.audio_bind;
        if (bind == "heads" && g_wav_heads)
        {
            sysAudio.playWAV(g_wav_heads, g_wav_heads_len, false);
            SYS_HAPTIC_DECODE();
        }
        else if (bind == "tails" && g_wav_tails)
        {
            sysAudio.playWAV(g_wav_tails, g_wav_tails_len, false);
            SYS_HAPTIC_DECODE();
        }
        else if (bind == "Ahab" && g_ahab_sound)
        {
            sysAudio.playWAV(g_ahab_sound, g_ahab_sound_len, false);
            SYS_HAPTIC_DECODE();
        }
        else if (bind == "none")
        {
            // 静默模式：不播放任何结尾音效
        }
        else
        {
            if (g_wav_final)
            {
                sysAudio.playWAV(g_wav_final, g_wav_final_len, false);
                SYS_HAPTIC_DECODE();
            }
            else
            {
                SYS_SOUND_SUCCESS_4BEEPS();
            }
        }
    }

    void executeDecodeSequence()
    {
        DrawResult res = sysSpecials.getResult();

        UIPrescript::PrepareLayoutFromRule(
            res.text.c_str(),
            appManager.getLanguage(),
            res.color,
            m_layout);

        startProcedureLoop();
        UIPrescript::PlayDecodeSequence(m_layout, sysConfig.decode_anim_style, PrescriptProcedureTick);

        m_scroll_offset = 0;
        drawDoneFrame();
        playFinalFeedback(res);
        SysAutoPush_ResetTimer();
    }

    bool rollAndMaybeRedirectSpecial()
    {
        sysSpecials.rollRandom();
        if (sysSpecials.getResult().is_special)
        {
            Prescript_Prepare_PreRolled();
            appManager.replace(AppId::PushNotify);
            return true;
        }
        return false;
    }

public:
    void onCreate() override
    {
        UIPrescript::InitGlitchPool();
        m_scroll_offset = 0;

        // 核心跳转：如果命运已定(弹窗进来的)，直接看结果；如果是手动进的，排队抽卡
        if (g_prescript_needs_roll)
            m_state = S_WAIT_RELEASE;
        else
            m_state = S_DECODE;
    }

    void onLoop() override
    {
        if (m_state == S_WAIT_RELEASE)
        {
            if (!HAL_Is_Key_Pressed())
            {
                if (rollAndMaybeRedirectSpecial()) return;
                m_state = S_CHAOS;
            }
        }
        else if (m_state == S_CHAOS)
        {
            drawChaosFrame();
            delay(ANIM_CHAOS_DELAY);
        }
        else if (m_state == S_DECODE)
        {
            executeDecodeSequence();
            m_state = S_DONE;
        }
    }

    void onDestroy() override
    {
        sysAudio.stopWAV();
        delay(5);
        g_prescript_needs_roll = true; // 退出时重置状态，保证下次手动进能正常抽卡
    }

    void onKnob(int delta) override
    {
        if (m_state != S_DONE) return;

        int maxVis = UIPrescript::MaxVisibleLines(m_layout.lang);
        if (m_layout.actualLines > maxVis)
        {
            int maxOffset = m_layout.actualLines - maxVis;
            int newOffset = m_scroll_offset + delta;
            if (newOffset < 0) newOffset = 0;
            if (newOffset > maxOffset) newOffset = maxOffset;
            if (newOffset != m_scroll_offset)
            {
                m_scroll_offset = newOffset;
                drawDoneFrame();
                SYS_SOUND_NAV();
            }
        }
    }

    void onKeyShort() override
    {
        if (m_state == S_CHAOS)
        {
            SYS_SOUND_NAV();
            m_state = S_DECODE;
        }
        else if (m_state == S_DONE)
        {
            SYS_SOUND_NAV();
            if (g_prescript_needs_roll)
            {
                if (rollAndMaybeRedirectSpecial()) return;
                m_state = S_WAIT_RELEASE;
            }
            else
            {
                appManager.popApp();
            }
        }
    }

    void onKeyLong() override
    {
        if (m_state == S_DONE || m_state == S_CHAOS)
            appManager.popApp();
    }

    void onBtn2Short() override
    {
        // 短按逻辑与旋钮按下完全一致：触发解码 / 抽取下一条
        onKeyShort();
    }

    void onBtn2Long() override
    {
        // 长按逻辑：清脆退出，返回上一级
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;
