/*
【模块职责】实现“双蛇杖”正式应用：入口放平校准、九武器随机教学、动作正误反馈、完成顺序语音、
“开始吧”解码提示，以及九拍Furioso限时动作流程。
【调用关系】AppManager只向本页分发统一按键和SysGesture语义；图片/PCM由SYS后台逐份预热，绘制由
UICaduceus和UIPrescript承担，震动通过sys_feedback语义接口触发。
【重要约束】本页不读取IMU、不复制识别阈值、不访问FATFS、不保存进度。普通阶段只有侧键短按
可以推进；旋钮主键短按和旋转均无效，主键/侧键长按仍退出。
*/
#include "sys/app_base.h"

#include <new>

#include <esp_heap_caps.h>

#include "lang/ui_strings.h"
#include "sys/app_manager.h"
#include "sys/sys_audio.h"
#include "sys/sys_caduceus_resources.h"
#include "sys/sys_config.h"
#include "sys/sys_feedback.h"
#include "sys/sys_gesture.h"
#include "sys/sys_res.h"
#include "ui/ui_caduceus.h"
#include "ui/ui_frame.h"
#include "ui/ui_prescript_decoder.h"
#include "ui/ui_theme.h"

namespace
{
    enum class CaduceusPageState : uint8_t
    {
        Loading = 0,
        Calibrating,
        Teaching,
        StartPrompt,
        FuriosoBeat,
        FuriosoGap,
        FuriosoFailed,
        FuriosoComplete,
    };

    enum CaduceusActionMask : uint8_t
    {
        ACTION_HORIZONTAL = 1U << 0,
        ACTION_VERTICAL = 1U << 1,
        ACTION_DIAGONAL_A = 1U << 2,
        ACTION_DIAGONAL_B = 1U << 3,
        ACTION_THRUST = 1U << 4,
        ACTION_UPPERCUT = 1U << 5,
    };

    constexpr uint8_t SLASH_ACTIONS = ACTION_HORIZONTAL | ACTION_VERTICAL |
                                      ACTION_DIAGONAL_A | ACTION_DIAGONAL_B;
    constexpr uint8_t ALL_ACTIONS = SLASH_ACTIONS | ACTION_THRUST | ACTION_UPPERCUT;
    constexpr uint8_t WEAPON_COUNT = UICaduceus::WEAPON_COUNT;
    constexpr uint8_t SCYTHE_WEAPON_INDEX = 5;
    constexpr uint16_t ALL_WEAPONS_COMPLETED = (1U << WEAPON_COUNT) - 1U;

    struct CaduceusWeaponConfig
    {
        CaduceusImageId image;
        uint8_t allowedActions;
        AudioAssetId fixedEffect;
    };

    /*
     * 固定编号按用户确认的图片顺序，并跳过特殊武器spoon：
     * 1斧、2锥、3大剑、4锤、5刺剑、6镰刀、7枪、8剑、9鞭。
     * Invalid表示本次进入时从四个通用音效中随机绑定一次；固定音效绝不进入其他武器的随机池。
     */
    constexpr CaduceusWeaponConfig WEAPONS[WEAPON_COUNT] = {
        {CaduceusImageId::Axe, SLASH_ACTIONS, AudioAssetId::Invalid},
        {CaduceusImageId::Cone, ACTION_THRUST, AudioAssetId::Invalid},
        {CaduceusImageId::GiantSword, ACTION_VERTICAL, AudioAssetId::CaduceusEffectGiantSword},
        {CaduceusImageId::Hammer, ACTION_VERTICAL, AudioAssetId::CaduceusEffectGiantSword},
        {CaduceusImageId::Rapier, ACTION_THRUST, AudioAssetId::Invalid},
        {CaduceusImageId::Scythe, SLASH_ACTIONS, AudioAssetId::CaduceusEffectScythe},
        {CaduceusImageId::Spear, ACTION_THRUST, AudioAssetId::Invalid},
        {CaduceusImageId::Sword, ALL_ACTIONS, AudioAssetId::Invalid},
        {CaduceusImageId::Whip, SLASH_ACTIONS, AudioAssetId::Invalid},
    };

    constexpr AudioAssetId GENERIC_EFFECTS[] = {
        AudioAssetId::CaduceusEffectSword1,
        AudioAssetId::CaduceusEffectSword2,
        AudioAssetId::CaduceusEffectSword3,
        AudioAssetId::CaduceusEffectWhip,
    };

    constexpr AudioAssetId CHANGE_EFFECTS[] = {
        AudioAssetId::CaduceusChange1,
        AudioAssetId::CaduceusChange2,
        AudioAssetId::CaduceusChange3,
    };

    constexpr AudioAssetId FIRST_COMPLETE_VOICES[WEAPON_COUNT] = {
        AudioAssetId::CaduceusFirst1,
        AudioAssetId::CaduceusFirst2,
        AudioAssetId::CaduceusFirst3,
        AudioAssetId::CaduceusFirst4,
        AudioAssetId::CaduceusFirst5,
        AudioAssetId::CaduceusFirst6,
        AudioAssetId::CaduceusFirst7,
        AudioAssetId::CaduceusFirst8,
        AudioAssetId::CaduceusFirst9,
    };

    static_assert(sizeof(WEAPONS) / sizeof(WEAPONS[0]) == WEAPON_COUNT,
                  "双蛇杖武器配置必须完整覆盖固定编号1~9");
    static_assert(sizeof(FIRST_COMPLETE_VOICES) / sizeof(FIRST_COMPLETE_VOICES[0]) == WEAPON_COUNT,
                  "首次完成语音必须完整覆盖完成顺序1~9");

    constexpr uint16_t FEEDBACK_FRAME_MS = UITheme::FRAME_FAST_MS;
    constexpr uint32_t TEACHING_INPUT_GUARD_US = 220000UL;

    /*
     * Furioso首版节奏参数以当前识别器500ms结算窗为下限设计：动作必须在0.9~1.4秒窗口内触发，
     * App再额外等待0.62秒让识别结果到达。结果判定使用事件的真实触发时间戳，因此等待期不会放宽窗口。
     * 节拍间隔先保守随机为0.28~0.62秒，实机验证连贯性后只需调整这里，不改状态机。
     */
    constexpr uint8_t FURIOSO_BEAT_COUNT = 9;
    constexpr uint16_t FURIOSO_WINDOW_MIN_MS = 900;
    constexpr uint16_t FURIOSO_WINDOW_MAX_MS = 1400;
    constexpr uint16_t FURIOSO_GAP_MIN_MS = 280;
    constexpr uint16_t FURIOSO_GAP_MAX_MS = 620;
    constexpr uint32_t FURIOSO_CLASSIFY_GRACE_US = 620000UL;

    uint8_t GestureActionMask(SysGestureType type)
    {
        switch (type)
        {
        case SysGestureType::HorizontalSlash: return ACTION_HORIZONTAL;
        case SysGestureType::VerticalSlash: return ACTION_VERTICAL;
        case SysGestureType::DiagonalSlashA: return ACTION_DIAGONAL_A;
        case SysGestureType::DiagonalSlashB: return ACTION_DIAGONAL_B;
        case SysGestureType::Thrust: return ACTION_THRUST;
        case SysGestureType::Uppercut: return ACTION_UPPERCUT;
        default: return 0;
        }
    }

    bool TimeReachedUs(uint32_t now, uint32_t deadline)
    {
        return (int32_t)(now - deadline) >= 0;
    }

    bool TimestampInWindow(uint32_t timestamp, uint32_t start, uint32_t end)
    {
        return (int32_t)(timestamp - start) >= 0 && (int32_t)(end - timestamp) >= 0;
    }
}

class AppCaduceus : public AppBase
{
private:
    CaduceusPageState state_ = CaduceusPageState::Loading;
    CaduceusPageState state_after_calibration_ = CaduceusPageState::Teaching;
    bool calibrated_ = false;
    bool current_weapon_solved_ = false;
    bool feedback_was_active_ = false;
    bool resource_unavailable_drawn_ = false;

    uint8_t current_weapon_ = 0;
    uint8_t furioso_beat_ = 0;
    uint8_t completion_count_ = 0;
    uint16_t completed_weapon_mask_ = 0;
    AudioAssetId session_effects_[WEAPON_COUNT] = {};

    uint32_t input_accept_after_us_ = 0;
    uint32_t furioso_beat_started_us_ = 0;
    uint32_t furioso_beat_deadline_us_ = 0;
    uint32_t furioso_next_beat_ms_ = 0;
    uint32_t last_frame_ms_ = 0;

    UICaduceusFeedbackAnimator feedback_animator_;
    /*
     * “开始吧”只在九把武器全部完成后短暂使用。TextLayout和解码动画器合计约7 KiB，
     * 若把它们作为全局App成员，会在设备上电时永久占用内部DRAM并挤压BLE/WiFi任务。
     * 因此动画器只在进入提示页时放入PSRAM，排版对象在begin复制完两行文本后立即释放。
     */
    UIPrescript::DecodeOverlayAnimator *start_animator_ = nullptr;

    static constexpr uint8_t OWNED_AUDIO_HANDLE_COUNT = 20;
    AudioHandle owned_audio_[OWNED_AUDIO_HANDLE_COUNT] = {};
    uint8_t next_audio_slot_ = 0;

    struct CompletionVoiceEntry
    {
        AudioHandle effect_handle = AUDIO_HANDLE_INVALID;
        AudioAssetId voice = AudioAssetId::Invalid;
    };
    CompletionVoiceEntry completion_voice_queue_[WEAPON_COUNT] = {};
    uint8_t completion_voice_head_ = 0;
    uint8_t completion_voice_count_ = 0;
    AudioHandle active_completion_voice_ = AUDIO_HANDLE_INVALID;

    void releaseStartAnimator()
    {
        if (!start_animator_)
            return;
        start_animator_->~DecodeOverlayAnimator();
        heap_caps_free(start_animator_);
        start_animator_ = nullptr;
    }

    bool prepareStartAnimator()
    {
        releaseStartAnimator();

        void *animatorStorage = heap_caps_malloc(
            sizeof(UIPrescript::DecodeOverlayAnimator),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!animatorStorage)
        {
            Serial.println("[双蛇杖] 无法申请开始提示动画PSRAM，改用静态文字兜底。");
            return false;
        }
        start_animator_ = new (animatorStorage) UIPrescript::DecodeOverlayAnimator();

        void *layoutStorage = heap_caps_malloc(sizeof(UIPrescript::TextLayout),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!layoutStorage)
        {
            Serial.println("[双蛇杖] 无法申请开始提示排版PSRAM，改用静态文字兜底。");
            releaseStartAnimator();
            return false;
        }

        UIPrescript::TextLayout *layout = new (layoutStorage) UIPrescript::TextLayout();
        UIPrescript::PrepareLayoutFromRule(UIStrings::CaduceusStartPrompt(language()),
                                           language(), TFT_CYAN, *layout);
        const int lineCount = min(UIPrescript::DecodeOverlayAnimator::MaxPageLines,
                                  layout->actualLines);
        start_animator_->begin(*layout, 0, lineCount,
                               sysConfig.decode_anim_style, millis());

        // begin()已经复制当前页文本；释放临时排版不会留下悬空指针。
        layout->~TextLayout();
        heap_caps_free(layoutStorage);
        return true;
    }

    SystemLang_t language() const
    {
        return appManager.getLanguage();
    }

    uint16_t teachingProgressMask() const
    {
        // 底部1~9表示本次流程的完成顺序，不表示固定武器ID；因此始终从数字1连续点亮。
        return completion_count_ == 0
                   ? 0
                   : (uint16_t)((1U << min(completion_count_, WEAPON_COUNT)) - 1U);
    }

    void rememberAudio(AudioHandle handle)
    {
        if (handle == AUDIO_HANDLE_INVALID)
            return;
        owned_audio_[next_audio_slot_] = handle;
        next_audio_slot_ = (uint8_t)((next_audio_slot_ + 1U) % OWNED_AUDIO_HANDLE_COUNT);
    }

    void stopOwnedAudio()
    {
        // 只停止本页创建的实例，不停止共享Effect/Voice总线，避免打断其他系统提示。
        for (AudioHandle &handle : owned_audio_)
        {
            if (handle != AUDIO_HANDLE_INVALID)
                sysAudio.stop(handle, 40);
            handle = AUDIO_HANDLE_INVALID;
        }
        next_audio_slot_ = 0;
        completion_voice_head_ = 0;
        completion_voice_count_ = 0;
        active_completion_voice_ = AUDIO_HANDLE_INVALID;
        for (CompletionVoiceEntry &entry : completion_voice_queue_)
            entry = CompletionVoiceEntry{};
    }

    AudioHandle playAsset(AudioAssetId asset, AudioBus bus)
    {
        if (asset == AudioAssetId::Invalid)
            return AUDIO_HANDLE_INVALID;
        AudioPlayOptions options;
        options.bus = bus;
        options.loopMode = AudioLoopMode::None;
        options.gain = 1.0f;
        const AudioHandle handle = sysAudio.play(asset, options);
        rememberAudio(handle);
        return handle;
    }

    /**
     * 首次完成台词必须排在该次武器音效之后。队列保留“音效句柄+完成次序台词”，
     * 即使用户很快按侧键进入下一把武器，也不会用固定毫秒延时猜测不同WAV的长度。
     */
    void queueCompletionVoice(AudioHandle effectHandle, AudioAssetId voice)
    {
        if (voice == AudioAssetId::Invalid)
            return;
        if (completion_voice_count_ >= WEAPON_COUNT)
        {
            Serial.println("[双蛇杖] 首次完成台词队列已满，本次台词无法排入。");
            return;
        }
        const uint8_t target = (uint8_t)((completion_voice_head_ + completion_voice_count_) %
                                         WEAPON_COUNT);
        completion_voice_queue_[target].effect_handle = effectHandle;
        completion_voice_queue_[target].voice = voice;
        ++completion_voice_count_;
    }

    void updateCompletionVoiceQueue()
    {
        // 同一时刻只顺序播放一条首次完成台词，避免快速推进时多句对白互相覆盖。
        if (active_completion_voice_ != AUDIO_HANDLE_INVALID)
        {
            if (sysAudio.isPlaying(active_completion_voice_))
                return;
            active_completion_voice_ = AUDIO_HANDLE_INVALID;
        }
        if (completion_voice_count_ == 0)
            return;

        CompletionVoiceEntry &entry = completion_voice_queue_[completion_voice_head_];
        if (entry.effect_handle != AUDIO_HANDLE_INVALID && sysAudio.isPlaying(entry.effect_handle))
            return;

        if (!sysAudio.hasAsset(entry.voice))
        {
            Serial.println("[双蛇杖] 首次完成台词资源不可用，已跳过本条队列。");
            entry = CompletionVoiceEntry{};
            completion_voice_head_ = (uint8_t)((completion_voice_head_ + 1U) % WEAPON_COUNT);
            --completion_voice_count_;
            return;
        }

        const AudioHandle voiceHandle = playAsset(entry.voice, AudioBus::Voice);
        if (voiceHandle == AUDIO_HANDLE_INVALID)
            return; // PCM槽暂满时留在队首，下个主循环继续尝试，不静默丢失首次台词。

        active_completion_voice_ = voiceHandle;
        entry = CompletionVoiceEntry{};
        completion_voice_head_ = (uint8_t)((completion_voice_head_ + 1U) % WEAPON_COUNT);
        --completion_voice_count_;
    }

    void randomizeSessionEffects()
    {
        for (uint8_t index = 0; index < WEAPON_COUNT; ++index)
        {
            if (WEAPONS[index].fixedEffect != AudioAssetId::Invalid)
            {
                session_effects_[index] = WEAPONS[index].fixedEffect;
            }
            else
            {
                session_effects_[index] = GENERIC_EFFECTS[random(
                    (long)(sizeof(GENERIC_EFFECTS) / sizeof(GENERIC_EFFECTS[0])))];
            }
        }
    }

    void playRandomChangeEffect()
    {
        const size_t count = sizeof(CHANGE_EFFECTS) / sizeof(CHANGE_EFFECTS[0]);
        playAsset(CHANGE_EFFECTS[random((long)count)], AudioBus::Effect);
    }

    void drawCalibrationFrame()
    {
        const int screenWidth = HAL_Get_Screen_Width();
        HAL_Sprite_Clear();
        drawAppWindow(UIStrings::CaduceusTitle(language()));
        const char *primary = UIStrings::CaduceusCalibrationPrompt(language());
        const char *secondary = UIStrings::CaduceusCalibrationDetail(language());
        const int primaryX = max(6, (screenWidth - HAL_Get_Text_Width(primary)) / 2);
        const int secondaryX = max(6, (screenWidth - HAL_Get_Text_Width(secondary)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(primaryX, 54, primary, 0.0f, TFT_YELLOW);
        HAL_Screen_ShowChineseLine(secondaryX, 80, secondary);
        UIFrame::DrawTip(UIStrings::HoldBackHint(language()));
        HAL_Screen_Update();
    }

    /**
     * 双蛇杖资源在系统进入主循环后逐份预热。本页只显示协调器状态，不直接打开FAT，
     * 这样资源仍由SYS持有，同时用户能在预热期间安全退出，不会进入动作或播放流程。
     */
    void drawResourceFrame()
    {
        const bool unavailable = SysRes_IsCaduceusUnavailable();
        const int screenWidth = HAL_Get_Screen_Width();
        HAL_Sprite_Clear();
        drawAppWindow(UIStrings::CaduceusTitle(language()));
        const char *primary = unavailable
                                  ? UIStrings::CaduceusResourceUnavailable(language())
                                  : UIStrings::CaduceusResourceLoading(language());
        const char *secondary = unavailable
                                    ? UIStrings::CaduceusResourceUnavailableDetail(language())
                                    : UIStrings::CaduceusResourceLoadingDetail(language());
        const int primaryX = max(6, (screenWidth - HAL_Get_Text_Width(primary)) / 2);
        const int secondaryX = max(6, (screenWidth - HAL_Get_Text_Width(secondary)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(primaryX, 54, primary, 0.0f,
                                                unavailable ? TFT_RED : TFT_YELLOW);
        HAL_Screen_ShowChineseLine(secondaryX, 80, secondary);
        UIFrame::DrawTip(UIStrings::HoldBackHint(language()));
        HAL_Screen_Update();
        resource_unavailable_drawn_ = unavailable;
    }

    void drawImageFrame(CaduceusImageId imageId, bool showTeachingProgress, uint32_t now)
    {
        const SysRgb565View image = SysCaduceusResources::GetImage(imageId);
        if (!image.valid())
        {
            UICaduceus::DrawMissingImage(UIStrings::CaduceusMissingImage(language()),
                                         teachingProgressMask(), feedback_animator_, now,
                                         showTeachingProgress);
        }
        else if (showTeachingProgress)
        {
            UICaduceus::DrawWeaponFrame(image, teachingProgressMask(), feedback_animator_, now);
        }
        else
        {
            UICaduceus::DrawImageOnlyFrame(image, feedback_animator_, now);
        }
        HAL_Screen_Update();
    }

    void drawCurrentFrame()
    {
        const uint32_t now = millis();
        last_frame_ms_ = now;
        switch (state_)
        {
        case CaduceusPageState::Loading:
            drawResourceFrame();
            break;
        case CaduceusPageState::Calibrating:
            drawCalibrationFrame();
            break;
        case CaduceusPageState::Teaching:
            drawImageFrame(WEAPONS[current_weapon_].image, true, now);
            break;
        case CaduceusPageState::StartPrompt:
            HAL_Sprite_Clear();
            if (start_animator_)
            {
                start_animator_->drawOverlay();
            }
            else
            {
                // PSRAM极端不足时仍允许用户看见并确认流程，不因动画资源失败卡死状态机。
                drawAppWindow(UIStrings::CaduceusTitle(language()));
                const char *prompt = UIStrings::CaduceusStartPrompt(language());
                const int x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(prompt)) / 2);
                HAL_Screen_ShowChineseLine_Faded_Color(x, 70, prompt, 0.0f, TFT_CYAN);
            }
            HAL_Screen_Update();
            break;
        case CaduceusPageState::FuriosoBeat:
        case CaduceusPageState::FuriosoGap:
        case CaduceusPageState::FuriosoComplete:
            drawImageFrame(WEAPONS[current_weapon_].image, false, now);
            break;
        case CaduceusPageState::FuriosoFailed:
            drawImageFrame(CaduceusImageId::Spoon, false, now);
            break;
        }
    }

    void beginCalibration(CaduceusPageState nextState)
    {
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        SysGesture_BeginCaduceusEntryCalibration();
        state_after_calibration_ = nextState;
        state_ = CaduceusPageState::Calibrating;
        calibrated_ = false;
        feedback_animator_.reset();
        feedback_was_active_ = false;
        drawCurrentFrame();
    }

    bool selectRandomRemainingWeapon()
    {
        uint8_t remaining[WEAPON_COUNT] = {};
        uint8_t remainingCount = 0;
        for (uint8_t index = 0; index < WEAPON_COUNT; ++index)
        {
            if ((completed_weapon_mask_ & (uint16_t)(1U << index)) == 0)
                remaining[remainingCount++] = index;
        }
        if (remainingCount == 0)
            return false;

        current_weapon_ = remaining[random((long)remainingCount)];
        current_weapon_solved_ = false;
        input_accept_after_us_ = micros() + TEACHING_INPUT_GUARD_US;
        return true;
    }

    void enterStartPrompt()
    {
        state_ = CaduceusPageState::StartPrompt;
        feedback_animator_.reset();
        (void)prepareStartAnimator();
        drawCurrentFrame();
    }

    void beginFuriosoBeat(uint8_t beat)
    {
        furioso_beat_ = beat;
        current_weapon_ = (beat + 1U == FURIOSO_BEAT_COUNT)
                              ? SCYTHE_WEAPON_INDEX
                              : (uint8_t)random((long)WEAPON_COUNT);
        state_ = CaduceusPageState::FuriosoBeat;
        feedback_animator_.reset();
        feedback_was_active_ = false;

        const uint32_t windowMs = (uint32_t)random((long)FURIOSO_WINDOW_MIN_MS,
                                                   (long)FURIOSO_WINDOW_MAX_MS + 1L);
        furioso_beat_started_us_ = micros();
        furioso_beat_deadline_us_ = furioso_beat_started_us_ + windowMs * 1000UL;
        drawCurrentFrame();
    }

    void beginFurioso()
    {
        beginFuriosoBeat(0);
    }

    void failFurioso()
    {
        if (state_ == CaduceusPageState::FuriosoFailed)
            return;
        state_ = CaduceusPageState::FuriosoFailed;
        feedback_animator_.trigger(TFT_RED, millis(), 440);
        feedback_was_active_ = true;
        Feedback_PlayCaduceusFuriosoFailure();
        drawCurrentFrame();
    }

    void completeFuriosoBeat()
    {
        Feedback_PlayCaduceusCorrect();
        playAsset(session_effects_[current_weapon_], AudioBus::Effect);
        feedback_animator_.trigger(TFT_CYAN, millis());
        feedback_was_active_ = true;

        if (furioso_beat_ + 1U >= FURIOSO_BEAT_COUNT)
        {
            // 最后一拍固定镰刀；成功后不增加结算文字，保持最后武器画面并只允许长按退出。
            state_ = CaduceusPageState::FuriosoComplete;
        }
        else
        {
            state_ = CaduceusPageState::FuriosoGap;
            furioso_next_beat_ms_ = millis() + (uint32_t)random(
                (long)FURIOSO_GAP_MIN_MS, (long)FURIOSO_GAP_MAX_MS + 1L);
        }
        drawCurrentFrame();
    }

    void handleTeachingGesture(uint8_t actionMask, const SysGestureEvent &event)
    {
        if (actionMask == 0 ||
            (int32_t)(event.timestamp_us - input_accept_after_us_) < 0)
            return;

        // 每次明确动作后保留短防抖，但不锁死已完成武器；用户可在切换前反复触发相关音效。
        input_accept_after_us_ = event.timestamp_us + TEACHING_INPUT_GUARD_US;

        if ((WEAPONS[current_weapon_].allowedActions & actionMask) == 0)
        {
            feedback_animator_.trigger(TFT_RED, millis());
            feedback_was_active_ = true;
            Feedback_PlayCaduceusWrong();
            drawCurrentFrame();
            return;
        }

        feedback_animator_.trigger(TFT_CYAN, millis());
        feedback_was_active_ = true;
        Feedback_PlayCaduceusCorrect();
        const AudioHandle effectHandle = playAsset(session_effects_[current_weapon_], AudioBus::Effect);

        if (!current_weapon_solved_)
        {
            /*
             * 只有当前武器第一次正确才推进步骤和登记首次台词。武器位图只负责避免九步内重复抽取；
             * UI数字由completion_count_生成连续1~N描边，不再把数字错误绑定到武器编号。
             */
            const uint8_t completionOrder = completion_count_;
            completed_weapon_mask_ |= (uint16_t)(1U << current_weapon_);
            current_weapon_solved_ = true;
            if (completion_count_ < WEAPON_COUNT)
                ++completion_count_;
            if (completionOrder < WEAPON_COUNT)
                queueCompletionVoice(effectHandle, FIRST_COMPLETE_VOICES[completionOrder]);
        }
        drawCurrentFrame();
    }

    void handleFuriosoGesture(uint8_t actionMask, const SysGestureEvent &event)
    {
        if (actionMask == 0 || state_ != CaduceusPageState::FuriosoBeat)
            return;

        // 识别结果可能在动作后约500ms才到达；只按事件携带的触发时间判断节拍窗口。
        if (!TimestampInWindow(event.timestamp_us,
                               furioso_beat_started_us_, furioso_beat_deadline_us_))
        {
            if (TimeReachedUs(event.timestamp_us, furioso_beat_deadline_us_))
                failFurioso();
            return;
        }

        if ((WEAPONS[current_weapon_].allowedActions & actionMask) != 0)
            completeFuriosoBeat();
        else
            failFurioso();
    }

public:
    void onCreate() override
    {
        releaseStartAnimator();
        completion_count_ = 0;
        completed_weapon_mask_ = 0;
        current_weapon_solved_ = false;
        furioso_beat_ = 0;
        stopOwnedAudio();
        randomizeSessionEffects();
        selectRandomRemainingWeapon();
        state_after_calibration_ = CaduceusPageState::Teaching;
        resource_unavailable_drawn_ = false;
        if (SysRes_IsCaduceusReady())
        {
            beginCalibration(CaduceusPageState::Teaching);
        }
        else
        {
            // 请求只缩短后台等待时间；真实文件读取仍由主循环中的SysRes_Update逐份执行。
            SysGesture_SetProfile(SysGestureProfile::Default);
            SysRes_RequestCaduceusPreload();
            state_ = CaduceusPageState::Loading;
            calibrated_ = false;
            drawCurrentFrame();
        }
    }

    void onResume() override
    {
        if (!SysRes_IsCaduceusReady())
        {
            SysGesture_SetProfile(SysGestureProfile::Default);
            SysRes_RequestCaduceusPreload();
            state_ = CaduceusPageState::Loading;
            calibrated_ = false;
            drawCurrentFrame();
            return;
        }

        CaduceusPageState resumeState = state_after_calibration_;
        if (state_ != CaduceusPageState::Calibrating &&
            state_ != CaduceusPageState::Loading)
            resumeState = state_;

        // Furioso是实时限时流程，后台停顿后不能从旧deadline继续；恢复时按失败处理并显示勺子。
        if (resumeState == CaduceusPageState::FuriosoBeat ||
            resumeState == CaduceusPageState::FuriosoGap)
            resumeState = CaduceusPageState::FuriosoFailed;
        beginCalibration(resumeState);
    }

    void onBackground() override
    {
        // 若后台打断发生在放平提示期间，保留原目标状态，不能把Calibrating本身写成恢复目标。
        if (state_ != CaduceusPageState::Calibrating &&
            state_ != CaduceusPageState::Loading)
            state_after_calibration_ = state_;
        SysGesture_SetProfile(SysGestureProfile::Default);
        stopOwnedAudio();
    }

    void onLoop() override
    {
        const uint32_t nowMs = millis();

        if (state_ == CaduceusPageState::Loading)
        {
            if (SysRes_IsCaduceusReady())
            {
                beginCalibration(state_after_calibration_);
            }
            else if (SysRes_IsCaduceusUnavailable() && !resource_unavailable_drawn_)
            {
                drawCurrentFrame();
            }
            return;
        }

        updateCompletionVoiceQueue();

        if (state_ == CaduceusPageState::Calibrating)
        {
            if (!SysGesture_IsCaduceusEntryCalibrationComplete())
                return;

            calibrated_ = true;
            if (state_after_calibration_ == CaduceusPageState::StartPrompt)
            {
                enterStartPrompt();
            }
            else
            {
                state_ = state_after_calibration_;
                input_accept_after_us_ = micros() + TEACHING_INPUT_GUARD_US;
                drawCurrentFrame();
            }
            return;
        }

        if (state_ == CaduceusPageState::StartPrompt)
        {
            if (start_animator_ && start_animator_->update(nowMs))
                drawCurrentFrame();
            return;
        }

        if (state_ == CaduceusPageState::FuriosoBeat)
        {
            const uint32_t timeout = furioso_beat_deadline_us_ + FURIOSO_CLASSIFY_GRACE_US;
            if (TimeReachedUs(micros(), timeout))
            {
                failFurioso();
                return;
            }
        }
        else if (state_ == CaduceusPageState::FuriosoGap &&
                 (int32_t)(nowMs - furioso_next_beat_ms_) >= 0)
        {
            beginFuriosoBeat((uint8_t)(furioso_beat_ + 1U));
            return;
        }

        // 边框消失后的第一帧也必须重绘一次，否则最后一层暗色边框会残留在静态图片上。
        const bool feedbackActive = feedback_animator_.active(nowMs);
        if ((feedbackActive || feedback_was_active_) && nowMs - last_frame_ms_ >= FEEDBACK_FRAME_MS)
        {
            drawCurrentFrame();
            feedback_was_active_ = feedbackActive;
        }
    }

    void onDestroy() override
    {
        stopOwnedAudio();
        releaseStartAnimator();
        feedback_animator_.reset();
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onKnob(int delta) override
    {
        (void)delta;
    }

    void onGesture(const SysGestureEvent &event) override
    {
        if (!calibrated_)
            return;
        const uint8_t actionMask = GestureActionMask(event.type);
        if (state_ == CaduceusPageState::Teaching)
            handleTeachingGesture(actionMask, event);
        else if (state_ == CaduceusPageState::FuriosoBeat)
            handleFuriosoGesture(actionMask, event);
    }

    void onKeyShort() override
    {
        // 产品规则明确只有侧键短按可以推进；旋钮主按键短按在本应用内始终无效。
    }

    void onBtn2Short() override
    {
        if (state_ == CaduceusPageState::Teaching)
        {
            if (!current_weapon_solved_)
                return;
            if (completion_count_ >= WEAPON_COUNT ||
                completed_weapon_mask_ == ALL_WEAPONS_COMPLETED)
            {
                enterStartPrompt();
                return;
            }

            playRandomChangeEffect();
            if (selectRandomRemainingWeapon())
            {
                feedback_animator_.reset();
                feedback_was_active_ = false;
                drawCurrentFrame();
            }
            return;
        }

        if (state_ == CaduceusPageState::StartPrompt)
        {
            // 第九把完成后的第一次侧键显示“开始吧”，本次侧键直接进入Furioso。
            beginFurioso();
            return;
        }

        if (state_ == CaduceusPageState::FuriosoFailed)
        {
            // 失败后勺子保持到用户确认；侧键按下结束本次应用，正常成功态仍要求长按退出。
            appManager.popApp();
        }
    }

    void onKeyLong() override
    {
        appManager.popApp();
    }

    void onBtn2Long() override
    {
        onKeyLong();
    }
};

AppCaduceus instanceCaduceus;
AppBase *appCaduceus = &instanceCaduceus;
