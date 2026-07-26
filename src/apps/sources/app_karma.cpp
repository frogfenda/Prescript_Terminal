/*
【模块职责】实现“业力”三模式木鱼页面：侧键/语义动作敲击、独立累计、旋钮按键进入模式选择、
向下旋转进入计数选择、清空确认和呼吸图片。
【调用关系】AppManager分发旋钮、主键、侧键与SysGesture事件；图片和音频均由SysRes启动预热，页面不直接访问FATFS。
【重要约束】三个计数只在本次App真正onDestroy退出且发生变化时写入公共配置；运行中和切换模式时不写LittleFS。
*/
#include "sys/app_manager.h"

#include <math.h>

#include "hal/hal.h"
#include "lang/ui_strings.h"
#include "sys/sys_audio.h"
#include "sys/sys_config.h"
#include "sys/sys_constants.h"
#include "sys/sys_gesture.h"
#include "sys/sys_karma_resources.h"
#include "ui/ui_floating_value_animator.h"
#include "ui/ui_frame.h"
#include "ui/ui_karma.h"
#include "ui/ui_value_animator.h"

namespace
{
    enum class KarmaPageState : uint8_t
    {
        Strike = 0,
        ModeSelect,
        CountSelected,
        ClearConfirm,
    };

    struct KarmaModePreset
    {
        AudioAssetId audio;
    };

    // 三个预设的文字目前都由UIStrings返回“业”，这里只绑定彼此独立的声音资源槽。
    constexpr KarmaModePreset KARMA_PRESETS[PrescriptConst::MAX_KARMA_MODES] = {
        {AudioAssetId::Karma1},
        {AudioAssetId::Karma2},
        {AudioAssetId::Karma3},
    };

    constexpr uint32_t BREATH_PERIOD_MS = 5000;
    constexpr uint32_t HIT_PULSE_MS = 260;
    constexpr uint16_t PAGE_FRAME_MS = UITheme::FRAME_NORMAL_MS;
    constexpr uint8_t OWNED_HANDLE_COUNT = 6;
}

class AppKarma : public AppBase
{
private:
    KarmaPageState state_ = KarmaPageState::Strike;
    uint8_t current_mode_ = 0;
    uint8_t mode_before_edit_ = 0;
    bool counts_dirty_ = false;

    uint16_t *image_buffer_ = nullptr;
    uint32_t entered_ms_ = 0;
    uint32_t last_frame_ms_ = 0;
    uint32_t hit_pulse_started_ms_ = 0;

    UIValueAnimator mode_animator_;
    UIFloatingValueAnimator count_pop_animator_;
    AudioHandle strike_handles_[OWNED_HANDLE_COUNT] = {};
    uint8_t next_handle_slot_ = 0;

    SystemLang_t language() const
    {
        return appManager.getLanguage();
    }

    void stopOwnedAudio()
    {
        // 木鱼是短促单次声音，但页面退出时仍只停止自己创建的实例，绝不停止整个Effect总线。
        for (AudioHandle &handle : strike_handles_)
        {
            if (handle != AUDIO_HANDLE_INVALID)
                sysAudio.stop(handle, 30);
            handle = AUDIO_HANDLE_INVALID;
        }
        next_handle_slot_ = 0;
    }

    void rememberHandle(AudioHandle handle)
    {
        if (handle == AUDIO_HANDLE_INVALID)
            return;
        strike_handles_[next_handle_slot_] = handle;
        next_handle_slot_ = (uint8_t)((next_handle_slot_ + 1) % OWNED_HANDLE_COUNT);
    }

    /**
     * 所有敲击来源的唯一业务入口。
     * 状态检查放在这里，保证侧键和未来两个动作在模式选择/清空确认期间都不会误累计或播放声音。
     */
    void performStrike()
    {
        if (state_ != KarmaPageState::Strike || current_mode_ >= PrescriptConst::MAX_KARMA_MODES)
            return;

        uint32_t &count = sysConfig.karma_counts[current_mode_];
        if (count < UINT32_MAX)
        {
            ++count;
            counts_dirty_ = true;
            count_pop_animator_.trigger("+1", TFT_RED);
        }

        hit_pulse_started_ms_ = millis();
        AudioPlayOptions options;
        options.bus = AudioBus::Effect;
        options.loopMode = AudioLoopMode::None;
        options.gain = 1.0f;
        rememberHandle(sysAudio.play(KARMA_PRESETS[current_mode_].audio, options));
        drawFrame();
    }

    void drawModeText(int sw)
    {
        const SystemLang_t lang = language();
        const char *prefix = UIStrings::KarmaModePrefix(lang);
        const char *name = UIStrings::KarmaModeName(lang, current_mode_);
        const char *suffix = UIStrings::KarmaModeSuffix(lang);
        const int width = HAL_Get_Text_Width(prefix) + HAL_Get_Text_Width(name) +
                          HAL_Get_Text_Width(suffix);
        const int x = max(8, sw - width - 10);
        const int y = 7;

        mode_animator_.drawSegmentedTextWithValueColor(x, y, prefix, name, suffix,
                                                       TFT_RED, 0.0f);
        if (state_ == KarmaPageState::ModeSelect)
        {
            // DrawCornerBox的center_y会在内部减2作为框顶，这里传文字顶部锚点，避免括号从行中间开始。
            UIFrame::DrawCornerBox(x - 5, min(sw - 4, x + width + 5),
                                   y - 2,
                                   0, HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 8,
                                   TFT_RED);
        }
    }

    void drawCountText(int sh)
    {
        const SystemLang_t lang = language();
        const char *prefix = UIStrings::KarmaCountPrefix(lang);
        const char *unit = UIStrings::KarmaCountUnit(lang);
        const char *name = UIStrings::KarmaCountName(lang);
        char countText[16];
        snprintf(countText, sizeof(countText), "%lu", (unsigned long)sysConfig.karma_counts[current_mode_]);

        const int y = sh - HAL_Get_Font_Line_Height(HAL_FONT_BODY) - 7;
        int x = 10;
        const int startX = x;
        HAL_Screen_ShowChineseLine(x, y, prefix);
        x += HAL_Get_Text_Width(prefix);
        HAL_Screen_ShowChineseLine(x, y, countText);
        const int popX = x;
        x += HAL_Get_Text_Width(countText);
        HAL_Screen_ShowChineseLine(x, y, unit);
        x += HAL_Get_Text_Width(unit);
        HAL_Screen_ShowChineseLine_Faded_Color(x, y, name, 0.0f, TFT_RED);
        x += HAL_Get_Text_Width(name);

        count_pop_animator_.draw(popX, y - 2);
        if (state_ == KarmaPageState::CountSelected)
        {
            // 与右上模式框使用同一顶部锚点，保证左下计数框也完整包住文字行。
            UIFrame::DrawCornerBox(startX - 5, x + 5,
                                   y - 2,
                                   0, HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 8,
                                   TFT_RED);
        }
    }

    void drawImage(int sw, int sh, uint32_t now)
    {
        if (!image_buffer_)
            return;

        const float phase = (float)((now - entered_ms_) % BREATH_PERIOD_MS) /
                            (float)BREATH_PERIOD_MS * TWO_PI;
        const float breathe = (sinf(phase) + 1.0f) * 0.5f;
        float scale = 0.90f + breathe * 0.07f;
        float brightness = 0.62f + breathe * 0.32f;

        // 每次敲击只叠加一个短促脉冲；慢呼吸相位持续推进，不会被反复敲击重置。
        if (hit_pulse_started_ms_ != 0)
        {
            const uint32_t elapsed = now - hit_pulse_started_ms_;
            if (elapsed < HIT_PULSE_MS)
            {
                const float impulse = 1.0f - (float)elapsed / (float)HIT_PULSE_MS;
                scale += impulse * 0.03f;
                brightness += impulse * 0.18f;
            }
            else
            {
                hit_pulse_started_ms_ = 0;
            }
        }
        if (scale > 1.0f) scale = 1.0f;
        if (brightness > 1.0f) brightness = 1.0f;

        const SysRgb565View image = SysKarmaResources::GetImage(current_mode_);
        const bool rendered = UIKarma::RenderBreathingImage(
            image_buffer_, UIKarma::IMAGE_CANVAS_SIDE, image, scale, brightness);
        const int imageX = (sw - UIKarma::IMAGE_CANVAS_SIDE) / 2;
        const int imageY = (sh - UIKarma::IMAGE_CANVAS_SIDE) / 2;
        HAL_Sprite_PushImage(imageX, imageY, UIKarma::IMAGE_CANVAS_SIDE,
                             UIKarma::IMAGE_CANVAS_SIDE, image_buffer_);

        if (!rendered)
        {
            const char *missing = UIStrings::KarmaMissingImage(language());
            const int textX = max(4, (sw - HAL_Get_Text_Width(missing)) / 2);
            HAL_Draw_Rect(imageX + 8, imageY + 8, UIKarma::IMAGE_CANVAS_SIDE - 16,
                          UIKarma::IMAGE_CANVAS_SIDE - 16, TFT_RED);
            HAL_Screen_ShowChineseLine_Faded_Color(textX, sh / 2 - 8, missing, 0.0f, TFT_RED);
        }
    }

    void drawFrame()
    {
        const uint32_t now = millis();
        const int sw = HAL_Get_Screen_Width();
        const int sh = HAL_Get_Screen_Height();
        HAL_Sprite_Clear();

        // 固定图层顺序：呼吸图片在底层，模式与计数在其上，危险确认弹窗最后覆盖。
        drawImage(sw, sh, now);
        drawModeText(sw);
        drawCountText(sh);
        if (state_ == KarmaPageState::ClearConfirm)
        {
            UIFrame::DrawDangerConfirm(UIStrings::KarmaClearTitle(language()),
                                       UIStrings::KarmaClearMessage(language()),
                                       UIStrings::KarmaClearHint(language()));
        }
        HAL_Screen_Update();
        last_frame_ms_ = now;
    }

public:
    void onCreate() override
    {
        // 业力页面前台期间才启用两种长边敲击；切换上下文会清除上一页面遗留的半截动作和事件。
        SysGesture_SetProfile(SysGestureProfile::Karma);
        state_ = KarmaPageState::Strike;
        current_mode_ = 0;
        mode_before_edit_ = 0;
        counts_dirty_ = false;
        entered_ms_ = millis();
        last_frame_ms_ = 0;
        hit_pulse_started_ms_ = 0;
        count_pop_animator_.reset();
        stopOwnedAudio();

        if (!image_buffer_)
        {
            image_buffer_ = (uint16_t *)ps_malloc(
                (size_t)UIKarma::IMAGE_CANVAS_SIDE * UIKarma::IMAGE_CANVAS_SIDE * sizeof(uint16_t));
        }
        if (!image_buffer_)
        {
            Serial.println("[业力] 呼吸图片缓冲区申请失败，已返回上一级。");
            appManager.popApp();
            return;
        }
        drawFrame();
    }

    void onResume() override
    {
        // 页面从弹窗或其他临时页面返回时恢复专属识别；后台期间不会继续累计业力。
        SysGesture_SetProfile(SysGestureProfile::Karma);
        drawFrame();
    }

    void onBackground() override
    {
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onLoop() override
    {
        // 呼吸动画持续运行，因此以统一30FPS重绘；两个文字动画仍各自推进内部衰减状态。
        mode_animator_.update();
        count_pop_animator_.update();
        const uint32_t now = millis();
        if (now - last_frame_ms_ >= PAGE_FRAME_MS)
            drawFrame();
    }

    void onDestroy() override
    {
        // 必须先恢复默认上下文并清队列，避免退出前最后一个敲击事件落到下一个页面。
        SysGesture_SetProfile(SysGestureProfile::Default);
        stopOwnedAudio();
        if (counts_dirty_)
        {
            // 用户已确认只在真正退出业力应用时保存；运行中敲击、切换模式和清空都只改内存。
            sysConfig.saveCommon();
            counts_dirty_ = false;
        }
        if (image_buffer_)
        {
            free(image_buffer_);
            image_buffer_ = nullptr;
        }
    }

    void onKnob(int delta) override
    {
        if (delta == 0 || state_ == KarmaPageState::ClearConfirm)
            return;

        if (state_ == KarmaPageState::Strike)
        {
            /*
             * 敲击状态只保留“向下旋转选择计数”。向上旋转不再进入模式选择；
             * 模式选择的唯一入口改为旋钮主按键，避免轻微滚动在两个角标间跳转。
             */
            if (delta < 0)
            {
                state_ = KarmaPageState::CountSelected;
                drawFrame();
            }
            return;
        }

        if (state_ == KarmaPageState::CountSelected)
            return;

        int next = ((int)current_mode_ + delta) % PrescriptConst::MAX_KARMA_MODES;
        if (next < 0) next += PrescriptConst::MAX_KARMA_MODES;
        current_mode_ = (uint8_t)next;
        mode_animator_.trigger(delta);
        count_pop_animator_.reset();
        drawFrame();
    }

    void onKeyShort() override
    {
        if (state_ == KarmaPageState::Strike)
        {
            // 旋钮主按键只进入模式选择，不触发木鱼；侧键和两个业力动作仍是敲击入口。
            mode_before_edit_ = current_mode_;
            state_ = KarmaPageState::ModeSelect;
            count_pop_animator_.reset();
            drawFrame();
            return;
        }

        if (state_ == KarmaPageState::ModeSelect)
        {
            state_ = KarmaPageState::Strike;
        }
        else if (state_ == KarmaPageState::CountSelected)
        {
            state_ = KarmaPageState::ClearConfirm;
        }
        else if (state_ == KarmaPageState::ClearConfirm)
        {
            if (sysConfig.karma_counts[current_mode_] != 0)
            {
                sysConfig.karma_counts[current_mode_] = 0;
                counts_dirty_ = true;
            }
            count_pop_animator_.reset();
            state_ = KarmaPageState::Strike;
        }
        drawFrame();
    }

    void onKeyLong() override
    {
        if (state_ == KarmaPageState::Strike)
        {
            appManager.popApp();
            return;
        }
        if (state_ == KarmaPageState::ModeSelect)
            current_mode_ = mode_before_edit_;
        state_ = KarmaPageState::Strike;
        count_pop_animator_.reset();
        drawFrame();
    }

    void onBtn2Short() override
    {
        if (state_ == KarmaPageState::Strike)
            performStrike();
        else
            onKeyShort();
    }

    void onBtn2Long() override
    {
        onKeyLong();
    }

    void onGesture(const SysGestureEvent &event) override
    {
        if (event.type == SysGestureType::KarmaStrikeA ||
            event.type == SysGestureType::KarmaStrikeB)
        {
            performStrike();
        }
    }
};

AppKarma instanceKarma;
AppBase *appKarma = &instanceKarma;
