// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_auto_push.h"
#include "sys_config.h"
#include "sys_audio.h"
#include "sys_haptic.h"
#include "sys_res.h"
#include "sys_specials.h"
#include "../hal/hal.h"
#include "../ui/ui_prescript_decoder.h"

/*
【模块职责】指令应用状态机。

本文件只负责“什么时候抽取指令、什么时候进入乱码态、什么时候确认解码、
完成后怎样播放音频/震动和返回页面”。指令文本排版、CHAOS 乱码帧、四种解码动画、
完成态正文和滚动条绘制全部下沉到 src/ui/ui_prescript_decoder.*。

这样 App 层不再保存屏幕尺寸、字体宽度、乱码字典和动画帧循环，后续改动画表现时只改 UI 层。
*/
bool g_prescript_needs_roll = true;

void Prescript_Prepare_PreRolled()
{
    g_prescript_needs_roll = false; // 被推送唤醒时调用，告诉自己不要重新摇号。
}

namespace {

// CHAOS 乱码态使用非阻塞帧刷新，避免等待确认期间卡住 AppManager 主循环。
static const uint32_t ANIM_CHAOS_DELAY_MS = 30;
// CHAOS 乱码音不再用固定 120ms 节拍；改为按屏幕乱码刷新帧的 2~4 倍随机触发。
// 这样声音和画面闪动保持同步，同时平均间隔约 90ms，比旧版略密一点。
static const uint8_t ANIM_CHAOS_SOUND_MIN_FRAMES = 2;
static const uint8_t ANIM_CHAOS_SOUND_MAX_FRAMES = 4;

/**
 * 解码动画帧间回调。
 * procedure.wav 存在时已经由 AppPrescript 以 loop=true 交给音频任务循环播放；
 * 如果资源未加载，则退回轻量 glitch 音，保持动画仍有反馈。
 */
void PrescriptProcedureTick()
{
    if (!g_wav_procedure)
        SYS_SOUND_GLITCH();
}

} // namespace

class AppPrescript : public AppBase
{
private:
    enum State
    {
        S_WAIT_RELEASE,
        S_CHAOS,
        S_DECODE,
        S_DONE
    };

    State m_state = S_WAIT_RELEASE;
    uint32_t m_last_chaos_frame_ms = 0;
    uint32_t m_last_chaos_sound_ms = 0;
    uint32_t m_next_chaos_sound_delay_ms = 0;
    int m_scroll_offset = 0;
    UIPrescript::TextLayout m_layout;

    /**
     * 启动 procedure 循环音。
     * 音频解码跑在 sys_audio 的后台任务中，UI 解码动画只负责逐帧触发回调/绘制。
     */
    void beginProcedureSound()
    {
        if (g_wav_procedure)
            sysAudio.playWAV(g_wav_procedure, g_wav_procedure_len, true);
    }

    /**
     * 根据特殊指令绑定播放收尾音效，同时触发解码成功震动。
     * 这里仍保留业务层判断：UI 层只画动画，不知道 heads/tails/Ahab 这些业务含义。
     */
    void playFinishFeedback(const DrawResult &res)
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
            // 静默指令：只停止 procedure，不播放结尾音效。
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

    /**
     * 执行一次完整解码。
     * App 层准备数据和声音，UI 层负责排版、动画和完成态绘制。
     */
    void executeDecodeSequence()
    {
        DrawResult res = sysSpecials.getResult();
        SystemLang_t lang = appManager.getLanguage();

        UIPrescript::PrepareLayoutFromRule(res.text.c_str(), lang, res.color, m_layout);
        m_scroll_offset = 0;

        beginProcedureSound();
        UIPrescript::PlayDecodeSequence(m_layout, sysConfig.decode_anim_style, PrescriptProcedureTick);
        UIPrescript::DrawDoneFrame(m_layout, m_scroll_offset);

        playFinishFeedback(res);
        SysAutoPush_ResetTimer();
    }

    /** 绘制完成态，并保持滚动逻辑只依赖 UI 层给出的可见行数。 */
    void drawDoneFrame()
    {
        UIPrescript::DrawDoneFrame(m_layout, m_scroll_offset);
    }

public:
    void onCreate() override
    {
        UIPrescript::InitGlitchPool();
        m_scroll_offset = 0;
        m_last_chaos_frame_ms = 0;
        m_last_chaos_sound_ms = 0;
        m_next_chaos_sound_delay_ms = 0;

        // 手动进入时先等待按键松开再抽取，避免进入页的一次短按被二次消费。
        // 推送/弹窗进入时命运已经由 sysSpecials 装载，直接进入解码。
        m_state = g_prescript_needs_roll ? S_WAIT_RELEASE : S_DECODE;
    }

    void onLoop() override
    {
        if (m_state == S_WAIT_RELEASE)
        {
            if (!HAL_Is_Key_Pressed())
            {
                sysSpecials.rollRandom();

                // 特殊指令先走 PushNotify 弹窗，再由弹窗确认进入本页解码。
                if (sysSpecials.getResult().is_special)
                {
                    Prescript_Prepare_PreRolled();
                    appManager.replace(AppId::PushNotify);
                    return;
                }

                m_state = S_CHAOS;
            }
        }
        else if (m_state == S_CHAOS)
        {
            uint32_t now = millis();
            DrawResult res = sysSpecials.getResult();

            if (now - m_last_chaos_frame_ms >= ANIM_CHAOS_DELAY_MS)
            {
                m_last_chaos_frame_ms = now;
                UIPrescript::DrawChaosFrame(appManager.getLanguage(), res.color);

                // 乱码音跟随屏幕乱码刷新帧随机触发，而不是独立固定节拍。
                // 只在画面真正闪动的帧上播放，避免声音和画面错拍。
                if (m_next_chaos_sound_delay_ms == 0 || now - m_last_chaos_sound_ms >= m_next_chaos_sound_delay_ms)
                {
                    m_last_chaos_sound_ms = now;
                    SYS_SOUND_GLITCH();
                    uint8_t frames = (uint8_t)random(ANIM_CHAOS_SOUND_MIN_FRAMES, ANIM_CHAOS_SOUND_MAX_FRAMES + 1);
                    m_next_chaos_sound_delay_ms = (uint32_t)frames * ANIM_CHAOS_DELAY_MS;
                }
            }
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
        g_prescript_needs_roll = true; // 退出时重置状态，保证下次手动进入能正常抽取。
    }

    void onKnob(int delta) override
    {
        if (m_state != S_DONE)
            return;

        int max_vis = UIPrescript::MaxVisibleLines(m_layout.lang);
        if (m_layout.actualLines <= max_vis)
            return;

        int max_offset = m_layout.actualLines - max_vis;
        int new_offset = m_scroll_offset + delta;
        if (new_offset < 0)
            new_offset = 0;
        if (new_offset > max_offset)
            new_offset = max_offset;

        if (new_offset != m_scroll_offset)
        {
            m_scroll_offset = new_offset;
            drawDoneFrame();
            SYS_SOUND_NAV();
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
                // 完成态短按只进入等待松手状态，真正抽取统一在 S_WAIT_RELEASE 中执行一次。
                m_scroll_offset = 0;
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
        // 副键短按和主键短按保持一致：确认解码 / 抽取下一条 / 返回。
        onKeyShort();
    }

    void onBtn2Long() override
    {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;
