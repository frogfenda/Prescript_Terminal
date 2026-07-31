/*
【模块职责】沉浸式“海”应用。读取 SysMotion 的共享六轴缓存，通过现有 MahonySolver 解算左右倾角，
再统一生成运动和天气帧输入，交给 UIFluidSurface 驱动具有惯性、雨滴涟漪和闪电反光的多层海面。
【分层边界】本 App 不访问 BSP/I2C，不复制手势识别逻辑；流体数值与底层绘制归 UIFluidSurface，
页面只管理传感器到视觉输入、生命周期、空闲计时和未来字幕最高层。
【交互约定】长按返回；旋钮和离散滚动手势在页面内不改变海面。明显的真实转动会刷新空闲计时，
设备静止后仍遵守全局待机设置。
*/
#include "sys/app_base.h"

#include <math.h>

#include "apps/app_sea.h"
#include "hal/hal.h"
#include "sys/app_manager.h"
#include "sys/sys_config.h"
#include "sys/sys_feedback.h"
#include "sys/sys_motion.h"
#include "sys/sys_narrative.h"
#include "sys/sys_pose_solver.h"
#include "sys/sys_sea_resources.h"
#include "ui/ui_clock.h"
#include "ui/ui_fluid_surface.h"
#include "ui/ui_prescript_decoder.h"
#include "ui/ui_theme.h"

namespace
{
    AppSeaAudioBinding g_sea_audio_binding = {};
    bool g_sea_rain_audio_active = false;
    String g_sea_narrative_audio_bind;
}

void AppSea_SetAudioBinding(const AppSeaAudioBinding *binding)
{
    g_sea_audio_binding = binding ? *binding : AppSeaAudioBinding{};
    if (g_sea_audio_binding.onRainStateChanged)
        g_sea_audio_binding.onRainStateChanged(g_sea_rain_audio_active);
    if (g_sea_audio_binding.onNarrativeLineChanged)
        g_sea_audio_binding.onNarrativeLineChanged(g_sea_narrative_audio_bind.c_str());
}

void AppSea_ClearAudioBinding()
{
    // 清除拥有者前先显式发送停止状态，避免外部雨声或对白继续播放却再也收不到退出通知。
    if (g_sea_audio_binding.onRainStateChanged)
        g_sea_audio_binding.onRainStateChanged(false);
    if (g_sea_audio_binding.onNarrativeLineChanged)
        g_sea_audio_binding.onNarrativeLineChanged("");
    g_sea_audio_binding = AppSeaAudioBinding{};
}

namespace
{
    constexpr float IMU_SAMPLE_HZ = 104.0f;
    constexpr uint32_t MOTION_STALE_MS = 160;
    constexpr uint32_t MOTION_DISCONTINUITY_US = 100000;
    constexpr float IDLE_ROLL_DELTA_DEG = 0.8f;
    constexpr float IDLE_ROLL_RATE_DPS = 14.0f;
    constexpr float IDLE_LINEAR_ACCEL_G = 0.075f;
    constexpr float LINEAR_ACCEL_DEAD_ZONE_G = 0.025f;
    constexpr float ANGULAR_ACCEL_FILTER_HZ = 18.0f;
    constexpr uint32_t IDLE_REFRESH_INTERVAL_MS = 350;
    constexpr uint32_t RAIN_INTENSITY_CHANGE_MIN_MS = 4500;
    constexpr uint32_t RAIN_INTENSITY_CHANGE_MAX_MS = 9000;
    constexpr uint32_t LIGHTNING_FIRST_MIN_MS = 6000;
    constexpr uint32_t LIGHTNING_FIRST_MAX_MS = 14000;
    constexpr uint32_t LIGHTNING_NEXT_MIN_MS = 8000;
    constexpr uint32_t LIGHTNING_NEXT_MAX_MS = 22000;

    float ClampFloat(float value, float low, float high)
    {
        if (value < low)
            return low;
        if (value > high)
            return high;
        return value;
    }

    /** 去除静止传感器噪声，同时保留超过阈值后的连续幅值，避免响应在阈值处突然跳变。 */
    float ApplyDeadZone(float value, float dead_zone)
    {
        if (fabsf(value) <= dead_zone)
            return 0.0f;
        return value > 0.0f ? value - dead_zone : value + dead_zone;
    }

    bool DeadlineReached(uint32_t now, uint32_t deadline)
    {
        return (int32_t)(now - deadline) >= 0;
    }
}

class AppSea : public AppBase
{
private:
    UIFluidSurface fluid_;
    SysPose::MahonySolver pose_solver_;
    uint32_t last_motion_sequence_ = 0;
    uint32_t last_motion_timestamp_us_ = 0;
    uint32_t last_motion_received_ms_ = 0;
    uint32_t last_physics_ms_ = 0;
    uint32_t last_render_ms_ = 0;
    uint32_t last_idle_refresh_ms_ = 0;
    UIFluidInput fluid_input_ = {};
    float previous_roll_rate_dps_ = 0.0f;
    float filtered_roll_accel_dps2_ = 0.0f;
    float previous_activity_roll_deg_ = 0.0f;
    bool pose_valid_ = false;
    bool rain_enabled_ = false;
    float rain_target_intensity_ = 0.0f;
    uint32_t next_rain_intensity_change_ms_ = 0;
    uint32_t next_lightning_ms_ = 0;
    uint32_t lightning_stage_deadline_ms_ = 0;
    uint32_t lightning_seed_ = 0;
    uint8_t lightning_stage_ = 0;
    float lightning_flash_ = 0.0f;
    UIPrescript::TextLayout narrative_layout_;
    UIPrescript::DecodeOverlayAnimator narrative_animator_;
    int narrative_scene_index_ = -1;
    size_t narrative_paragraph_index_ = 0;
    size_t narrative_line_index_ = 0;
    int narrative_page_first_line_ = 0;
    int narrative_page_line_count_ = 0;
    uint32_t narrative_last_glitch_ms_ = 0;
    bool narrative_available_ = false;

    void notifyRainAudio(bool active)
    {
        g_sea_rain_audio_active = active;
        if (g_sea_audio_binding.onRainStateChanged)
            g_sea_audio_binding.onRainStateChanged(active);
    }

    /**
     * 把当前句子的逻辑音频标识交给外部绑定层。全局 String 保证注册回调后能够立即获得当前状态，
     * 回调接收的 c_str() 只在调用期间有效；空字符串统一表示停止上一句对白。
     */
    void notifyNarrativeAudio(const String &audioBind)
    {
        g_sea_narrative_audio_bind = audioBind;
        if (g_sea_audio_binding.onNarrativeLineChanged)
            g_sea_audio_binding.onNarrativeLineChanged(g_sea_narrative_audio_bind.c_str());
    }

    const SysNarrativeScene *currentNarrativeScene() const
    {
        return SysSeaResources::Narrative().scene(narrative_scene_index_);
    }

    const SysNarrativeParagraph *currentNarrativeParagraph() const
    {
        const SysNarrativeScene *scene = currentNarrativeScene();
        if (!scene || narrative_paragraph_index_ >= scene->paragraphs.size())
            return nullptr;
        return &scene->paragraphs[narrative_paragraph_index_];
    }

    const SysNarrativeLine *currentNarrativeLine() const
    {
        const SysNarrativeParagraph *paragraph = currentNarrativeParagraph();
        if (!paragraph || narrative_line_index_ >= paragraph->lines.size())
            return nullptr;
        return &paragraph->lines[narrative_line_index_];
    }

    /**
     * 启动当前句子的指定分页。正常指令排版器已经完成 UTF-8 换行，这里只按每页最多两行切片；
     * 第一行始终锚定屏幕中线，第二行向下排列，超过两行的内容由下一次按键翻页。
     */
    bool beginNarrativePage()
    {
        if (narrative_page_first_line_ < 0 ||
            narrative_page_first_line_ >= narrative_layout_.actualLines)
            return false;

        narrative_page_line_count_ = min(UIPrescript::DecodeOverlayAnimator::MaxPageLines,
                                         narrative_layout_.actualLines - narrative_page_first_line_);
        narrative_animator_.begin(narrative_layout_, narrative_page_first_line_,
                                  narrative_page_line_count_, sysConfig.decode_anim_style, millis());
        narrative_last_glitch_ms_ = 0;
        return narrative_animator_.isActive();
    }

    /**
     * 使用指令页的 UTF-8 排版器准备当前句子。音频绑定只在一句话第一次进入时触发，
     * 同一句因宽度产生的第二页不会重复通知或重新播放。
     */
    bool beginCurrentNarrativeSentence(bool notifyAudio)
    {
        const SysNarrativeParagraph *paragraph = currentNarrativeParagraph();
        const SysNarrativeLine *line = currentNarrativeLine();
        if (!paragraph || !line)
            return false;

        UIPrescript::PrepareLayoutFromRule(line->text.c_str(), appManager.getLanguage(),
                                           paragraph->color, narrative_layout_);
        if (narrative_layout_.actualLines <= 0)
            return false;

        narrative_page_first_line_ = 0;
        if (!beginNarrativePage())
            return false;
        if (notifyAudio)
            notifyNarrativeAudio(line->audioBind);
        return true;
    }

    /** 选择一个不同于上一场景的带权随机场景，并从首段首句开始播放。 */
    bool beginNextNarrativeScene()
    {
        const int nextScene = SysSeaResources::Narrative().chooseWeightedScene(narrative_scene_index_);
        if (nextScene < 0)
        {
            narrative_available_ = false;
            notifyNarrativeAudio("");
            return false;
        }

        narrative_scene_index_ = nextScene;
        narrative_paragraph_index_ = 0;
        narrative_line_index_ = 0;
        narrative_available_ = beginCurrentNarrativeSentence(true);
        return narrative_available_;
    }

    /**
     * 按“句子的下一页（每页最多两行）→段落下一句→场景下一段→新的随机场景”推进。
     * 这里不重新实现按键消抖；主键和侧键短按都由 AppManager 汇合到 onKeyShort()。
     */
    void advanceNarrative()
    {
        if (!narrative_available_)
        {
            beginNextNarrativeScene();
            return;
        }

        if (narrative_animator_.isRunning())
        {
            narrative_animator_.finishImmediately();
            return;
        }

        const int nextPageFirstLine = narrative_page_first_line_ + narrative_page_line_count_;
        if (nextPageFirstLine < narrative_layout_.actualLines)
        {
            narrative_page_first_line_ = nextPageFirstLine;
            beginNarrativePage();
            return;
        }

        const SysNarrativeParagraph *paragraph = currentNarrativeParagraph();
        const SysNarrativeScene *scene = currentNarrativeScene();
        if (!paragraph || !scene)
        {
            beginNextNarrativeScene();
            return;
        }

        if (narrative_line_index_ + 1 < paragraph->lines.size())
        {
            ++narrative_line_index_;
            beginCurrentNarrativeSentence(true);
            return;
        }

        if (narrative_paragraph_index_ + 1 < scene->paragraphs.size())
        {
            ++narrative_paragraph_index_;
            narrative_line_index_ = 0;
            beginCurrentNarrativeSentence(true);
            return;
        }

        beginNextNarrativeScene();
    }

    void triggerLightning(uint32_t now)
    {
        lightning_seed_ = (uint32_t)random(1, 0x7FFFFFFF);
        lightning_stage_ = 1;
        lightning_flash_ = 1.0f;
        lightning_stage_deadline_ms_ = now + 55;
        next_lightning_ms_ = now + (uint32_t)random(LIGHTNING_NEXT_MIN_MS, LIGHTNING_NEXT_MAX_MS + 1);

        // 当前不播放声音；回调保留未来接入 thunder.wav 或程序化雷声的延迟信息。
        if (g_sea_audio_binding.onThunder)
        {
            const uint8_t intensity = (uint8_t)constrain((int)lroundf(rain_target_intensity_ * 100.0f), 0, 100);
            const uint16_t delay_ms = (uint16_t)random(260, 1000);
            g_sea_audio_binding.onThunder(intensity, delay_ms);
        }
    }

    void updateWeather()
    {
        if (!rain_enabled_)
        {
            rain_target_intensity_ = 0.0f;
            lightning_flash_ = 0.0f;
            lightning_stage_ = 0;
            return;
        }

        const uint32_t now = millis();
        if (DeadlineReached(now, next_rain_intensity_change_ms_))
        {
            rain_target_intensity_ = (float)random(45, 91) / 100.0f;
            next_rain_intensity_change_ms_ = now +
                                             (uint32_t)random(RAIN_INTENSITY_CHANGE_MIN_MS,
                                                              RAIN_INTENSITY_CHANGE_MAX_MS + 1);
        }

        if (lightning_stage_ != 0 && DeadlineReached(now, lightning_stage_deadline_ms_))
        {
            if (lightning_stage_ == 1 && random(100) < 22)
            {
                // 少数雷击有第二次较弱闪光；第二闪光不重新触发声音回调。
                lightning_stage_ = 2;
                lightning_flash_ = 0.34f;
                lightning_stage_deadline_ms_ = now + 65;
            }
            else
            {
                lightning_stage_ = 0;
                lightning_flash_ = 0.0f;
            }
        }
        else if (lightning_stage_ == 0 && DeadlineReached(now, next_lightning_ms_))
        {
            triggerLightning(now);
        }
    }

    UIFluidFrameInput buildFluidFrameInput() const
    {
        UIFluidFrameInput frame = {};
        frame.motion = fluid_input_;
        frame.motion.valid = pose_valid_ &&
                             millis() - last_motion_received_ms_ <= MOTION_STALE_MS;
        frame.weather.raining = rain_enabled_;
        frame.weather.rain_intensity = rain_target_intensity_;
        frame.weather.lightning_flash = lightning_flash_;
        frame.weather.lightning_seed = lightning_seed_;
        return frame;
    }

    /**
     * 消费 SysMotion 最新样本并更新姿态；sequence 保证一个样本只进入 Mahony 一次。
     * 采样中断超过 100ms 时重置解算器，避免 Light Sleep、I2C 恢复或后台停顿后的旧积分污染姿态。
     */
    void updateMotionInput()
    {
        SysMotionSample sample = {};
        if (!SysMotion_GetLatest(&sample) || sample.sequence == last_motion_sequence_)
            return;

        last_motion_sequence_ = sample.sequence;
        if (!sample.gyro_fresh)
            return;

        const uint32_t previous_timestamp_us = last_motion_timestamp_us_;
        const bool discontinuity = previous_timestamp_us != 0 &&
                                   sample.timestamp_us - previous_timestamp_us > MOTION_DISCONTINUITY_US;
        if (discontinuity)
        {
            pose_solver_.Begin(IMU_SAMPLE_HZ);
            pose_valid_ = false;
            filtered_roll_accel_dps2_ = 0.0f;
        }
        last_motion_timestamp_us_ = sample.timestamp_us;

        float sample_dt_seconds = 1.0f / IMU_SAMPLE_HZ;
        if (previous_timestamp_us != 0 && !discontinuity)
        {
            sample_dt_seconds = ClampFloat((float)(sample.timestamp_us - previous_timestamp_us) / 1000000.0f,
                                           0.002f,
                                           0.050f);
        }

        // 海的现有实机参数仍基于传感器原生轴；坐标底座改造期间显式保留旧语义，后续单独回归迁移。
        pose_solver_.Update(sample.sensor_imu);
        const SysPose::Result pose = pose_solver_.GetResult(false);
        if (!pose.valid)
            return;

        fluid_input_.roll_deg = pose.euler.rollDeg;
        fluid_input_.roll_rate_dps = sample.sensor_imu.gxDps;

        /*
         * 角加速度使用相邻真实 IMU 时间戳求导，再做一次快速低通。它只用于“停止转动后的反向回摆”，
         * 不参与姿态解算；采样中断后的第一帧清零，避免把长时间间隔误判成一次巨大冲击。
         */
        const float raw_roll_accel = (previous_timestamp_us == 0 || discontinuity)
                                         ? 0.0f
                                         : (sample.sensor_imu.gxDps - previous_roll_rate_dps_) / sample_dt_seconds;
        const float accel_alpha = ClampFloat(sample_dt_seconds * ANGULAR_ACCEL_FILTER_HZ, 0.0f, 1.0f);
        filtered_roll_accel_dps2_ += (raw_roll_accel - filtered_roll_accel_dps2_) * accel_alpha;
        fluid_input_.roll_accel_dps2 = filtered_roll_accel_dps2_;
        previous_roll_rate_dps_ = sample.sensor_imu.gxDps;

        if (sample.accel_fresh)
        {
            /*
             * Mahony 四元数已经描述机身姿态，可直接计算机身 Y/Z 轴应看到的单位重力分量。
             * 实测加速度减去该分量后得到线性运动，避免“只是倾斜设备”被重复当成横向冲击。
             */
            const SysPose::Quaternion &q = pose.quaternion;
            const float expected_gravity_y = 2.0f * (q.w * q.x + q.y * q.z);
            const float expected_gravity_z = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
            fluid_input_.lateral_accel_g = ApplyDeadZone(sample.sensor_imu.ayG - expected_gravity_y,
                                                         LINEAR_ACCEL_DEAD_ZONE_G);
            fluid_input_.vertical_accel_g = ApplyDeadZone(sample.sensor_imu.azG - expected_gravity_z,
                                                          LINEAR_ACCEL_DEAD_ZONE_G);
        }

        fluid_input_.valid = true;
        pose_valid_ = true;
        last_motion_received_ms_ = millis();

        /*
         * 连续姿态不是 AppManager 的实体按键/旋钮事件，必须由页面主动声明“用户仍在交互”。
         * 只对角度变化或真实转速响应，并限频刷新，避免静止噪声让设备永远无法自动待机。
         */
        const float roll_delta = fabsf(fluid_input_.roll_deg - previous_activity_roll_deg_);
        const float linear_activity = fabsf(fluid_input_.lateral_accel_g) +
                                      fabsf(fluid_input_.vertical_accel_g);
        const uint32_t now = millis();
        if ((roll_delta >= IDLE_ROLL_DELTA_DEG ||
             fabsf(fluid_input_.roll_rate_dps) >= IDLE_ROLL_RATE_DPS ||
             linear_activity >= IDLE_LINEAR_ACCEL_G) &&
            now - last_idle_refresh_ms_ >= IDLE_REFRESH_INTERVAL_MS)
        {
            appManager.resetIdleTimer();
            last_idle_refresh_ms_ = now;
            previous_activity_roll_deg_ = fluid_input_.roll_deg;
        }
    }

    /**
     * 最高前景层只绘制叙事解码器当前帧。DecodeOverlayAnimator 不拥有背景也不推屏，
     * 因此海面、雨滴、闪电、泡沫先完成绘制后，文字可以稳定处于最上层且不参与流体形变。
     */
    void drawForegroundOverlay()
    {
        if (narrative_available_)
            narrative_animator_.drawOverlay();
    }

    /** 清空并按固定图层顺序绘制一帧，整帧最后只推屏一次。 */
    void drawFrame()
    {
        HAL_Sprite_Clear();
        fluid_.draw();
        drawForegroundOverlay();
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        // 绑定器独立持有雨声句柄；Sea 页面只确保它已经安装，不直接依赖 SysAudio。
        AppSeaAudio_EnsureInstalled();
        pose_solver_.Begin(IMU_SAMPLE_HZ);
        fluid_.reset(HAL_Get_Screen_Width(), HAL_Get_Screen_Height());
        UIPrescript::InitGlitchPool();
        last_motion_sequence_ = 0;
        last_motion_timestamp_us_ = 0;
        last_motion_received_ms_ = 0;
        last_physics_ms_ = millis();
        last_render_ms_ = 0;
        last_idle_refresh_ms_ = 0;
        fluid_input_ = {};
        previous_roll_rate_dps_ = 0.0f;
        filtered_roll_accel_dps2_ = 0.0f;
        previous_activity_roll_deg_ = 0.0f;
        pose_valid_ = false;

        /*
         * 叙事目录已经由SysRes在开机或语言切换时解析完成；进入Sea只选择场景，
         * 不再访问FATFS或解析JSON。首次场景使用带权随机，后续排除当前索引避免连续重复。
         */
        narrative_paragraph_index_ = 0;
        narrative_line_index_ = 0;
        narrative_page_first_line_ = 0;
        narrative_page_line_count_ = 0;
        narrative_last_glitch_ms_ = 0;
        narrative_available_ = !SysSeaResources::Narrative().empty();
        if (narrative_available_)
            beginNextNarrativeScene();
        else
            notifyNarrativeAudio("");

        const uint32_t now = millis();
        rain_enabled_ = random(100) < 30;
        rain_target_intensity_ = rain_enabled_ ? 0.58f : 0.0f;
        next_rain_intensity_change_ms_ = now + (uint32_t)random(RAIN_INTENSITY_CHANGE_MIN_MS,
                                                                  RAIN_INTENSITY_CHANGE_MAX_MS + 1);
        next_lightning_ms_ = now + (uint32_t)random(LIGHTNING_FIRST_MIN_MS,
                                                    LIGHTNING_FIRST_MAX_MS + 1);
        lightning_stage_deadline_ms_ = 0;
        lightning_seed_ = 0;
        lightning_stage_ = 0;
        lightning_flash_ = 0.0f;
        notifyRainAudio(rain_enabled_);
    }

    void onResume() override
    {
        // 返回前台时从当前时间重新累计，不补算后台期间的流体步骤。
        last_physics_ms_ = millis();
        last_render_ms_ = 0;
        last_motion_sequence_ = 0;
        next_lightning_ms_ = millis() + (uint32_t)random(LIGHTNING_FIRST_MIN_MS,
                                                         LIGHTNING_FIRST_MAX_MS + 1);
        lightning_stage_ = 0;
        lightning_flash_ = 0.0f;
        notifyRainAudio(rain_enabled_);
        drawFrame();
    }

    void onBackground() override
    {
        // 页面不可见时停止注入最后一次动态输入；SysMotion 仍由系统主循环统一维护。
        fluid_input_.roll_rate_dps = 0.0f;
        fluid_input_.roll_accel_dps2 = 0.0f;
        fluid_input_.lateral_accel_g = 0.0f;
        fluid_input_.vertical_accel_g = 0.0f;
        fluid_input_.valid = false;
        lightning_stage_ = 0;
        lightning_flash_ = 0.0f;
        notifyRainAudio(false);
        notifyNarrativeAudio("");
    }

    void onLoop() override
    {
        updateMotionInput();
        updateWeather();

        const uint32_t now = millis();
        const bool narrativeFrameChanged = narrative_available_ && narrative_animator_.update(now);
        if (narrativeFrameChanged && narrative_animator_.isRunning() &&
            now - narrative_last_glitch_ms_ >= 90)
        {
            // 复用现有故障短音/轻触反馈；它走 SFX 队列，不会替换未来由句子 audio 绑定的 WAV。
            narrative_last_glitch_ms_ = now;
            Feedback_PlayGlitch();
        }

        if (now - last_physics_ms_ >= UITheme::FRAME_FAST_MS)
        {
            const float dt_seconds = (float)(now - last_physics_ms_) / 1000.0f;
            last_physics_ms_ = now;
            fluid_.update(dt_seconds, buildFluidFrameInput());
        }

        // 物理以约 60Hz 推进，整屏旋转/QSPI 刷新锁定约 30FPS，兼顾波动连续性和 TE 等待成本。
        if (UIClock_Due(last_render_ms_, UITheme::FRAME_NORMAL_MS))
            drawFrame();
    }

    void onDestroy() override
    {
        notifyRainAudio(false);
        notifyNarrativeAudio("");
    }

    // Sea 页面不把实体旋钮或全局摇动滚动映射为业务操作，避免倾斜观看时误改状态。
    void onKnob(int delta) override { (void)delta; }

    void onKeyShort() override
    {
        Feedback_PlayKnobTick();
        advanceNarrative();
    }

    void onKeyLong() override
    {
        Feedback_PlayBack();
        appManager.popApp();
    }
};

AppSea instanceSea;
AppBase *appSea = &instanceSea;
