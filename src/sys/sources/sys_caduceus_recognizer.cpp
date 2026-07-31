/*
【模块职责】实现双蛇杖专属的静止门控、动作起手局部坐标、固定窗口特征提取和六分类。
【算法来源】局部坐标和相位字段与tools/imu/action_frame.py、caduceus_replay.py同步；分类优先
使用主旋转轴相对起手重力的几何关系，采集数据只辅助确定小抖动下限和特殊轨迹方向。
【重要约束】动作期间只用陀螺仪推进相对姿态，不让高冲击加速度修正重力；模糊/不完整波形
必须拒识。本文件不读取I2C、不分配堆内存，也不接触App、UI、声音或震动。
*/
#include "sys/sys_caduceus_recognizer.h"

#include <math.h>

#include "sys/sys_action_frame.h"

namespace
{
    using SysActionFrame::Vector3;

    // 静止平均必须排除真实动作前导；旧220dps/±1g会把自然起手误写成陀螺仪零偏。
    constexpr float QUIET_GYRO_DPS = 120.0f;
    constexpr float QUIET_ACCEL_DELTA_G = 0.25f;
    constexpr uint32_t QUIET_ARM_US = 70000;
    constexpr float TRIGGER_GYRO_DPS = 300.0f;
    constexpr float TRIGGER_ACCEL_DELTA_G = 1.80f;
    constexpr uint32_t WINDOW_POST_US = 500000;
    constexpr uint16_t QUIET_AVERAGE_SAMPLES = 32;

    // 以下候选阈值必须与Python classify()同步；任何改动都要先重跑完整标注正/负样本。
    constexpr float THRUST_MAX_PRIMARY_AREA_DEG = 70.0f;
    constexpr float THRUST_MAX_PHASE_MS = 320.0f;
    constexpr float THRUST_MIN_LINEAR_ACCEL_G = 3.0f;
    constexpr float THRUST_MIN_TRAJECTORY_SPEED_GS = 0.20f;
    constexpr float THRUST_MIN_RETURN_RATIO = 0.05f;
    constexpr float THRUST_MAX_PRIMARY_DPS = 950.0f;

    constexpr float CUT_MIN_PRIMARY_PEAK_DPS = 300.0f;
    constexpr float CUT_MIN_POSE_ANGLE_DEG = 35.0f;
    constexpr float CUT_MAX_PRIMARY_AREA_DEG = 520.0f;
    constexpr float CUT_MIN_LINEAR_ACCEL_G = 1.5f;
    constexpr float CUT_MIN_TRAJECTORY_SPEED_GS = 0.38f;
    constexpr float CUT_STRONG_LINEAR_ACCEL_G = 4.0f;
    constexpr float CUT_MAX_RETURN_RATIO = 0.80f;

    constexpr float UPPERCUT_MIN_VERTICAL_ALIGNMENT = 0.25f;
    constexpr float HORIZONTAL_MIN_VERTICAL_ALIGNMENT = -0.50f;
    constexpr float VERTICAL_MAX_VERTICAL_ALIGNMENT = -0.88f;

    // 96帧覆盖约0.9秒@104Hz，足够容纳静止前导、500ms后窗和已知30～49ms短空洞。
    constexpr uint8_t SAMPLE_RING_CAPACITY = SysActionFrame::MAX_PHASE_SAMPLES;

    struct Features
    {
        SysActionFrame::PhaseFeatures phase;
        float gravity_parallel = 0.0f;
        float trajectory_vertical_alignment = 0.0f;
        float trajectory_xz_product = 0.0f;
    };

    SysActionFrame::Integrator s_action_frame;
    SysActionFrame::FrameSample s_samples[SAMPLE_RING_CAPACITY] = {};
    SysActionFrame::FrameSample s_linear_samples[SAMPLE_RING_CAPACITY] = {};
    uint8_t s_sample_head = 0;
    uint8_t s_sample_count = 0;
    uint32_t s_quiet_since_us = 0;
    Vector3 s_quiet_accel_sum = {};
    Vector3 s_quiet_gyro_sum = {};
    uint16_t s_quiet_count = 0;
    bool s_frame_ready = false;
    bool s_armed = false;
    bool s_collecting = false;
    uint32_t s_trigger_us = 0;
    Vector3 s_trigger_gravity = {};

    float GyroMagnitude(const SysPose::ImuSample &imu)
    {
        return sqrtf(imu.gxDps * imu.gxDps + imu.gyDps * imu.gyDps + imu.gzDps * imu.gzDps);
    }

    float AccelMagnitude(const SysPose::ImuSample &imu)
    {
        return sqrtf(imu.axG * imu.axG + imu.ayG * imu.ayG + imu.azG * imu.azG);
    }

    bool DeadlineReached(uint32_t now, uint32_t deadline)
    {
        return (int32_t)(now - deadline) >= 0;
    }

    void ClearSamples()
    {
        s_sample_head = 0;
        s_sample_count = 0;
        for (auto &sample : s_samples)
            sample = {};
    }

    void AppendSample(const SysActionFrame::FrameSample &sample)
    {
        uint8_t target = 0;
        if (s_sample_count < SAMPLE_RING_CAPACITY)
        {
            target = (uint8_t)((s_sample_head + s_sample_count) % SAMPLE_RING_CAPACITY);
            ++s_sample_count;
        }
        else
        {
            target = s_sample_head;
            s_sample_head = (uint8_t)((s_sample_head + 1) % SAMPLE_RING_CAPACITY);
        }
        s_samples[target] = sample;
    }

    bool UpdateActionFrame(const SysMotionSample &sample)
    {
        SysActionFrame::FrameSample local = {};
        if (!s_action_frame.Update(sample.timestamp_us, sample.body_imu, &local))
            return false;
        AppendSample(local);
        return true;
    }

    void ResetQuietEvidence()
    {
        s_quiet_since_us = 0;
        s_quiet_accel_sum = {};
        s_quiet_gyro_sum = {};
        s_quiet_count = 0;
    }

    void AddQuietSample(const SysMotionSample &sample)
    {
        if (!sample.accel_fresh)
            return;

        /*
         * 只保留约300ms的近期均值。达到32帧后按指数式滑窗削弱旧样本，既避免长时间静置
         * 累加溢出，也让起手前轻微握姿变化能更新重力和零偏。
         */
        if (s_quiet_count >= QUIET_AVERAGE_SAMPLES)
        {
            const float keep = (float)(QUIET_AVERAGE_SAMPLES - 1) /
                               (float)QUIET_AVERAGE_SAMPLES;
            s_quiet_accel_sum.x *= keep;
            s_quiet_accel_sum.y *= keep;
            s_quiet_accel_sum.z *= keep;
            s_quiet_gyro_sum.x *= keep;
            s_quiet_gyro_sum.y *= keep;
            s_quiet_gyro_sum.z *= keep;
            s_quiet_count = QUIET_AVERAGE_SAMPLES - 1;
        }
        s_quiet_accel_sum.x += sample.body_imu.axG;
        s_quiet_accel_sum.y += sample.body_imu.ayG;
        s_quiet_accel_sum.z += sample.body_imu.azG;
        s_quiet_gyro_sum.x += sample.body_imu.gxDps;
        s_quiet_gyro_sum.y += sample.body_imu.gyDps;
        s_quiet_gyro_sum.z += sample.body_imu.gzDps;
        ++s_quiet_count;
    }

    bool AnchorFromQuiet(const SysMotionSample &sample)
    {
        if (s_quiet_count < 3)
            return false;
        const float inverse_count = 1.0f / (float)s_quiet_count;
        const Vector3 gravity = SysActionFrame::Unit({
            s_quiet_accel_sum.x * inverse_count,
            s_quiet_accel_sum.y * inverse_count,
            s_quiet_accel_sum.z * inverse_count,
        });
        const Vector3 gyro_bias = {
            s_quiet_gyro_sum.x * inverse_count,
            s_quiet_gyro_sum.y * inverse_count,
            s_quiet_gyro_sum.z * inverse_count,
        };
        if (!s_action_frame.Reset(gravity, gyro_bias))
            return false;

        /*
         * 静止期间每个fresh加速度帧都可重锚，确保动作从最近握姿的单位相对姿态开始。清掉的
         * 只是静止历史；第一帧运动到触发峰的前导仍会按真实dt进入同一个局部坐标。
         */
        ClearSamples();
        s_frame_ready = true;
        s_armed = true;
        s_trigger_gravity = gravity;
        return UpdateActionFrame(sample);
    }

    bool LinearizeSamples()
    {
        if (s_sample_count == 0)
            return false;
        for (uint8_t index = 0; index < s_sample_count; ++index)
        {
            s_linear_samples[index] =
                s_samples[(uint8_t)((s_sample_head + index) % SAMPLE_RING_CAPACITY)];
        }
        return true;
    }

    bool ExtractFeatures(Features &features)
    {
        if (!LinearizeSamples() || SysActionFrame::Norm(s_trigger_gravity) < 0.9f)
            return false;
        if (!SysActionFrame::ExtractPhase(
                s_linear_samples, s_sample_count, s_trigger_us, &features.phase))
            return false;

        const Vector3 &main_axis = features.phase.main_axis_local;
        features.gravity_parallel = SysActionFrame::Dot(main_axis, s_trigger_gravity);

        const Vector3 trajectory_direction =
            SysActionFrame::Unit(features.phase.trajectory_peak_velocity_local_gs);
        if (SysActionFrame::Norm(trajectory_direction) < 0.9f)
            return false;

        /*
         * BodyY才是设备顶底长轴。屏幕朝手心/朝外等价于绕BodyY翻转180°，X/Z同时反号；
         * 轨迹与重力点积、Y分量及X*Z乘积都保持不变。这里直接提取这些物理标量，既不猜
         * “掌心在哪边”，也不会在设备竖直时构造一个无法观测的伪朝向基底。
        */
        features.trajectory_vertical_alignment =
            SysActionFrame::Dot(trajectory_direction, s_trigger_gravity);
        features.trajectory_xz_product =
            trajectory_direction.x * trajectory_direction.z;
        return true;
    }

    SysGestureType Classify(const Features &features, int8_t &direction)
    {
        direction = 0;
        const SysActionFrame::PhaseFeatures &phase = features.phase;
        // 突刺优先按“较小角位移+明确平移+收回”判断；翻面后不再要求某个横轴固定正负。
        if (phase.primary_area_deg <= THRUST_MAX_PRIMARY_AREA_DEG &&
            phase.main_duration_ms <= THRUST_MAX_PHASE_MS &&
            phase.linear_accel_peak_g >= THRUST_MIN_LINEAR_ACCEL_G &&
            phase.trajectory_peak_speed_gs >= THRUST_MIN_TRAJECTORY_SPEED_GS &&
            phase.return_to_primary_ratio >= THRUST_MIN_RETURN_RATIO &&
            phase.primary_peak_dps <= THRUST_MAX_PRIMARY_DPS)
        {
            direction = 1;
            return SysGestureType::Thrust;
        }

        // 斩击共用较短的完成角；回位波形只作为有限辅助，避免整段回位再次触发。
        const bool complete_cut =
            phase.primary_peak_dps >= CUT_MIN_PRIMARY_PEAK_DPS &&
            phase.max_main_relative_angle_deg >= CUT_MIN_POSE_ANGLE_DEG &&
            phase.primary_area_deg <= CUT_MAX_PRIMARY_AREA_DEG &&
            phase.linear_accel_peak_g >= CUT_MIN_LINEAR_ACCEL_G &&
            (phase.trajectory_peak_speed_gs >= CUT_MIN_TRAJECTORY_SPEED_GS ||
             phase.linear_accel_peak_g >= CUT_STRONG_LINEAR_ACCEL_G) &&
            phase.return_to_primary_ratio <= CUT_MAX_RETURN_RATIO;
        if (!complete_cut)
            return SysGestureType::None;

        const float vertical = features.trajectory_vertical_alignment;
        if (vertical >= UPPERCUT_MIN_VERTICAL_ALIGNMENT)
        {
            direction = -1;
            return SysGestureType::Uppercut;
        }
        if (vertical >= HORIZONTAL_MIN_VERTICAL_ALIGNMENT)
        {
            direction = features.gravity_parallel >= 0.0f ? 1 : -1;
            return SysGestureType::HorizontalSlash;
        }
        if (vertical <= VERTICAL_MAX_VERTICAL_ALIGNMENT)
        {
            direction = 1;
            return SysGestureType::VerticalSlash;
        }

        // 两条下行斜线使用BodyY翻面后仍不变的X*Z无向斜率区分；A/B物理命名待实机确认。
        direction = 1;
        return features.trajectory_xz_product < 0.0f
                   ? SysGestureType::DiagonalSlashA
                   : SysGestureType::DiagonalSlashB;
    }
}

void SysCaduceusRecognizer_Reset()
{
    ClearSamples();
    for (auto &sample : s_linear_samples)
        sample = {};
    ResetQuietEvidence();
    s_frame_ready = false;
    s_armed = false;
    s_collecting = false;
    s_trigger_us = 0;
    s_trigger_gravity = {};
}

bool SysCaduceusRecognizer_Update(const SysMotionSample &sample, SysGestureEvent *out_event)
{
    if (!out_event || !sample.gyro_fresh)
        return false;

    // 已锚定后每个fresh陀螺仪帧都必须推进局部姿态；100ms真断点由工具拒绝并整体重置。
    if (s_frame_ready && !UpdateActionFrame(sample))
    {
        SysCaduceusRecognizer_Reset();
        return false;
    }

    const float gyro_magnitude = GyroMagnitude(sample.body_imu);
    const float accel_delta = fabsf(AccelMagnitude(sample.body_imu) - 1.0f);
    const bool quiet = gyro_magnitude < QUIET_GYRO_DPS && accel_delta < QUIET_ACCEL_DELTA_G;

    if (quiet)
    {
        if (s_quiet_since_us == 0)
        {
            s_quiet_since_us = sample.timestamp_us;
            s_quiet_accel_sum = {};
            s_quiet_gyro_sum = {};
            s_quiet_count = 0;
        }
        AddQuietSample(sample);
        if (!s_collecting && sample.accel_fresh &&
            sample.timestamp_us - s_quiet_since_us >= QUIET_ARM_US)
        {
            if (!AnchorFromQuiet(sample))
            {
                s_frame_ready = false;
                s_armed = false;
            }
        }
    }
    else
    {
        ResetQuietEvidence();
        if (s_collecting)
        {
            // 收窗期间保持当前局部坐标，但下一动作必须重新静止锚定。
            s_armed = false;
        }
        else
        {
            const bool trigger = gyro_magnitude >= TRIGGER_GYRO_DPS ||
                                 accel_delta >= TRIGGER_ACCEL_DELTA_G;
            if (s_frame_ready && s_armed && trigger)
            {
                s_collecting = true;
                s_trigger_us = sample.timestamp_us;
                s_armed = false;
            }
            // armed后未到高触发线的自然起手继续保留，避免弱前导吞掉真正主峰。
        }
    }

    if (!s_collecting || !DeadlineReached(sample.timestamp_us, s_trigger_us + WINDOW_POST_US))
        return false;

    Features features = {};
    int8_t direction = 0;
    SysGestureType type = SysGestureType::None;
    if (ExtractFeatures(features))
        type = Classify(features, direction);

    const uint32_t event_timestamp_us = s_trigger_us;
    s_collecting = false;
    s_trigger_us = 0;
    s_frame_ready = false;
    s_trigger_gravity = {};
    ClearSamples();
    if (type == SysGestureType::None)
        return false;

    out_event->type = type;
    out_event->timestamp_us = event_timestamp_us;
    out_event->strength_dps = features.phase.gyro_peak_dps;
    out_event->direction = direction;
    return true;
}
