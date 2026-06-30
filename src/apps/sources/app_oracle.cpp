/*
【模块职责】纺织机应用。

交互逻辑：
- 进入后显示一个缓慢呼吸的问句；
- 旋钮在“获取纺织机回复”和“吃什么？”之间切换；
- 短按后按 type 从 sys_oracle 抽取文本，并复用当前指令解码动画显示；
- 结果页再短按一次回到主菜单。

注意：本 App 不经过 sysSpecials，不会写入特殊指令当前结果，避免污染特殊音频绑定和人物链进度。
*/
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_config.h"
#include "sys/sys_oracle.h"
#include "sys/sys_audio.h"
#include "sys/sys_res.h"
#include "sys/sys_feedback.h"
#include "ui/ui_prescript_decoder.h"
#include "ui/ui_theme.h"
#include "lang/ui_strings.h"
#include <math.h>

namespace
{

    static const uint16_t ORACLE_COLOR = TFT_CYAN;
    static const uint32_t ASK_FRAME_MS = 33;
    static const uint32_t BREATH_PERIOD_MS = 3000;

    uint16_t scaleColor565(uint16_t color, float scale)
    {
        if (scale < 0.0f)
            scale = 0.0f;
        if (scale > 1.0f)
            scale = 1.0f;

        int r = (color >> 11) & 0x1F;
        int g = (color >> 5) & 0x3F;
        int b = color & 0x1F;
        r = (int)(r * scale);
        g = (int)(g * scale);
        b = (int)(b * scale);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    float breatheLevel(uint32_t now)
    {
        float phase = (float)(now % BREATH_PERIOD_MS) / (float)BREATH_PERIOD_MS;
        float wave = 0.5f - 0.5f * cosf(phase * 6.2831853f);
        return 0.34f + 0.66f * wave;
    }

    void drawCenteredLine(const char *text, int y, HALFontRole role, uint16_t color)
    {
        int sw = HAL_Get_Screen_Width();
        int w = HAL_Get_Text_Width_Font(text, role);
        int x = (sw - w) / 2;
        if (x < 0)
            x = 0;
        HAL_Screen_ShowLine_Font(x, y, text, role, color);
    }

} // namespace

class AppOracle : public AppBase
{
private:
    enum State : uint8_t
    {
        S_ASK_IDLE,
        S_DECODING,
        S_RESULT
    };

    State m_state = S_ASK_IDLE;
    uint8_t m_selected = 0; // 0: weaver, 1: food
    uint32_t m_last_frame_ms = 0;
    const char *m_question = nullptr;
    OracleAnswer m_answer;
    UIPrescript::TextLayout m_layout;

    const char *selectedType() const
    {
        return m_selected == 0 ? "weaver" : "food";
    }

    const char *optionText(uint8_t index) const
    {
        return UIStrings::OracleOption(appManager.getLanguage(), index);
    }

    void chooseQuestion()
    {
        SystemLang_t lang = appManager.getLanguage();
        m_question = UIStrings::OracleQuestion(lang, random(UIStrings::OracleQuestionCount(lang)));
    }

    void drawOption(uint8_t index, int x, int y)
    {
        const char *text = optionText(index);
        bool selected = (m_selected == index);
        int w = HAL_Get_Text_Width_Font(text, HAL_FONT_BODY);
        int h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        uint16_t color = selected ? ORACLE_COLOR : UITheme::COLOR_DIM;

        if (selected)
        {
            HAL_Draw_Rect(x - 7, y - 5, w + 14, h + 8, ORACLE_COLOR);
            HAL_Fill_Triangle(x - 13, y + h / 2 - 4, x - 7, y + h / 2, x - 13, y + h / 2 + 4, ORACLE_COLOR);
        }

        HAL_Screen_ShowLine_Font(x, y, text, HAL_FONT_BODY, color);
    }

    void drawAskFrame()
    {
        uint32_t now = millis();
        HAL_Sprite_Clear();

        if (!m_question)
            chooseQuestion();

        uint16_t qColor = scaleColor565(ORACLE_COLOR, breatheLevel(now));
        int lineH = HAL_Get_Font_Line_Height(HAL_FONT_BODY);

        // 问句本身保持在屏幕视觉中心。底部选项作为独立操作区，
        // 不再把问句整体往上挤，避免开屏时文字看起来偏高。
        int questionY = (HAL_Get_Screen_Height() - lineH) / 2;
        drawCenteredLine(m_question, questionY, HAL_FONT_BODY, qColor);

        // 底部两个选项略微上移，并改为整体居中排布。
        // 这样保留“左 / 右”选择关系，但不会像贴两侧边缘那样间隔过大。
        int optionY = HAL_Get_Screen_Height() - lineH - 20;
        const char *leftText = optionText(0);
        const char *rightText = optionText(1);
        int leftW = HAL_Get_Text_Width_Font(leftText, HAL_FONT_BODY);
        int rightW = HAL_Get_Text_Width_Font(rightText, HAL_FONT_BODY);
        int optionGap = 60;
        int groupW = leftW + optionGap + rightW;
        int leftX = (HAL_Get_Screen_Width() - groupW) / 2;
        if (leftX < 12)
            leftX = 12;
        int rightX = leftX + leftW + optionGap;

        drawOption(0, leftX, optionY);
        drawOption(1, rightX, optionY);

        HAL_Screen_Update();
    }

    void beginProcedureSound()
    {
        if (g_wav_procedure)
            sysAudio.playWAV(g_wav_procedure, g_wav_procedure_len, true);
    }

    static void procedureTick()
    {
        if (!g_wav_procedure)
            SYS_SOUND_GLITCH();
    }

    void executeDecode()
    {
        SystemLang_t lang = appManager.getLanguage();
        UIPrescript::PrepareLayoutFromRule(m_answer.text.c_str(), lang, ORACLE_COLOR, m_layout);

        beginProcedureSound();
        UIPrescript::PlayDecodeSequence(m_layout, sysConfig.decode_anim_style, procedureTick);
        UIPrescript::DrawDoneFrame(m_layout, 0);

        sysAudio.stopWAV();
        if (g_wav_final)
            sysAudio.playWAV(g_wav_final, g_wav_final_len, false);
        else
            Feedback_PlayDecodeComplete();

        m_state = S_RESULT;
    }

    void drawResultFrame()
    {
        UIPrescript::DrawDoneFrame(m_layout, 0);
    }

public:
    void onCreate() override
    {
        UIPrescript::InitGlitchPool();
        m_selected = 0;
        m_last_frame_ms = 0;
        chooseQuestion();
        m_state = S_ASK_IDLE;
        drawAskFrame();
    }

    void onResume() override
    {
        if (m_state == S_ASK_IDLE)
            drawAskFrame();
        else if (m_state == S_RESULT)
            drawResultFrame();
    }

    void onLoop() override
    {
        if (m_state == S_ASK_IDLE)
        {
            uint32_t now = millis();
            if (now - m_last_frame_ms >= ASK_FRAME_MS)
            {
                m_last_frame_ms = now;
                drawAskFrame();
            }
        }
        else if (m_state == S_DECODING)
        {
            executeDecode();
        }
    }

    void onDestroy() override
    {
        sysAudio.stopWAV();
    }

    void onKnob(int delta) override
    {
        if (m_state != S_ASK_IDLE || delta == 0)
            return;

        m_selected = (m_selected == 0) ? 1 : 0;
        SYS_SOUND_NAV();
        drawAskFrame();
    }

    void onKeyShort() override
    {
        if (m_state == S_ASK_IDLE)
        {
            if (!sysOracle.drawByType(selectedType(), appManager.getLanguage(), m_answer))
                Serial.println("[纺织机] 指定类型没有可用答案，使用兜底文本。 ");

            SYS_SOUND_CONFIRM();
            m_state = S_DECODING;
        }
        else if (m_state == S_RESULT)
        {
            // 答案显示完成后，短按返回纺织机自己的入口页，
            // 方便连续问询；长按仍然返回系统主菜单。
            sysAudio.stopWAV();
            SYS_SOUND_NAV();
            m_selected = 0;
            chooseQuestion();
            m_state = S_ASK_IDLE;
            drawAskFrame();
        }
    }

    void onKeyLong() override
    {
        Feedback_PlayBack();
        appManager.launch(AppId::MainMenu);
    }

    void onBtn2Short() override { onKeyShort(); }
    void onBtn2Long() override { onKeyLong(); }
};

AppOracle instanceOracle;
AppBase *appOracle = &instanceOracle;
