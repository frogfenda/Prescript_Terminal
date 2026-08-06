/*
【模块职责】实现双蛇杖可移植识别核心。固件和tools/imu宿主回放共同编译本文件，所有分段
状态、分类顺序、置信度和自适应收窗条件只有这一份真相源。
【连续策略】首次动作必须严格静止锚定；成功动作可在20秒内继承预测重力/零偏。收窗期间若
出现“峰→持续谷→新峰”，第二候选从当前姿态快照独立开始，避免丢失紧邻动作。
【提前结算】轨迹积分固定到触发后300ms，主峰搜索到350ms；因此只在360ms后、信号稳定
至少50ms且当前分类边界分数足够高时提前完成。模糊/拒识仍等满500ms，安全边界不被缩短。
*/
#include "sys/sys_caduceus_core.h"

#include <math.h>

namespace SysCaduceusCore
{
    namespace
    {
        using SysActionFrame::Vector3;

        constexpr float QUIET_GYRO_DPS = 120.0f;
        constexpr float QUIET_ACCEL_DELTA_G = 0.25f;
        constexpr uint32_t QUIET_ARM_US = 70000;
        constexpr uint32_t ENTRY_CALIBRATION_MIN_US = 1000000;
        constexpr uint16_t QUIET_AVERAGE_SAMPLES = 32;

        constexpr float TRIGGER_GYRO_DPS = 300.0f;
        constexpr float TRIGGER_ACCEL_DELTA_G = 1.00f;
        constexpr uint32_t PRE_TRIGGER_US = 100000;
        constexpr uint32_t HARD_WINDOW_POST_US = 500000;
        constexpr uint32_t ADAPTIVE_MIN_POST_US = 360000;
        constexpr uint32_t ADAPTIVE_SETTLE_HOLD_US = 50000;
        constexpr float ADAPTIVE_SETTLED_ENERGY = 0.55f;
        constexpr float ADAPTIVE_MIN_CONFIDENCE = 0.72f;
        constexpr uint32_t MAX_CHAIN_US = 20000000;
        constexpr uint32_t MIN_CANDIDATE_INTERVAL_US = 120000;
        constexpr uint32_t VALLEY_HOLD_US = 30000;
        constexpr float REARM_MIN_ENERGY = 1.15f;
        constexpr float VALLEY_RATIO = 0.55f;
        constexpr float VALLEY_FLOOR = 0.70f;

        constexpr float THRUST_MAX_PRIMARY_AREA_DEG = 70.0f;
        constexpr float THRUST_MAX_PHASE_MS = 450.0f;
        constexpr float THRUST_MIN_LINEAR_ACCEL_G = 1.8f;
        constexpr float THRUST_MIN_TRAJECTORY_SPEED_GS = 0.12f;
        constexpr float THRUST_MAX_PRIMARY_DPS = 950.0f;
        constexpr float THRUST_MAX_POSE_ANGLE_DEG = 90.0f;
        constexpr float THRUST_MAX_GRAVITY_PARALLEL = -0.55f;
        constexpr float THRUST_MIN_LONGITUDINAL_AXIS = 0.35f;

        constexpr float CUT_MIN_PRIMARY_PEAK_DPS = 300.0f;
        constexpr float CUT_MIN_POSE_ANGLE_DEG = 35.0f;
        constexpr float CUT_MAX_PRIMARY_AREA_DEG = 520.0f;
        constexpr float CUT_MIN_LINEAR_ACCEL_G = 1.5f;
        constexpr float CUT_MIN_TRAJECTORY_SPEED_GS = 0.38f;
        constexpr float CUT_STRONG_LINEAR_ACCEL_G = 4.0f;
        constexpr float CUT_MAX_RETURN_RATIO = 0.80f;

        constexpr float UPPERCUT_MIN_VERTICAL_ALIGNMENT = 0.25f;
        constexpr float UPPERCUT_MAX_ABS_GRAVITY_PARALLEL = 0.35f;
        constexpr float UPPERCUT_MAX_ABS_LONGITUDINAL_AXIS = 0.30f;
        constexpr float UPPERCUT_MIN_LONGITUDINAL_ACCEL_RATIO = 0.25f;
        constexpr float HORIZONTAL_MIN_VERTICAL_ALIGNMENT = -0.50f;
        constexpr float VERTICAL_MAX_VERTICAL_ALIGNMENT = -0.88f;

        float Clamp01(float value)
        {
            return fmaxf(0.0f, fminf(1.0f, value));
        }

        float ScoreAbove(float value, float threshold, float full_margin)
        {
            return Clamp01((value - threshold) / fmaxf(full_margin, 0.000001f));
        }

        float ScoreBelow(float value, float threshold, float full_margin)
        {
            return Clamp01((threshold - value) / fmaxf(full_margin, 0.000001f));
        }

        float Min2(float left, float right)
        {
            return fminf(left, right);
        }

        float Min4(float a, float b, float c, float d)
        {
            return fminf(fminf(a, b), fminf(c, d));
        }

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
            return static_cast<int32_t>(now - deadline) >= 0;
        }

        float MotionEnergy(float gyro, float accel_delta)
        {
            return fmaxf(gyro / TRIGGER_GYRO_DPS, accel_delta / TRIGGER_ACCEL_DELTA_G);
        }

        bool EntryGravityMatchesScreenUp(const Vector3 &gravity)
        {
            return gravity.z >= 0.70f && fabsf(gravity.x) <= 0.45f && fabsf(gravity.y) <= 0.45f;
        }

        float AcceptedConfidence(float evidence, float separation)
        {
            /*
             * 该分数只表达离物理边界的相对距离，不声称是统计概率。0.5表示刚好越过全部硬门，
             * 证据和类别分离度各自提高分数；提前结算另要求>=0.72，临界候选继续等待硬上限。
             */
            return Clamp01(0.50f + 0.35f * Clamp01(evidence) + 0.15f * Clamp01(separation));
        }

    }

    void Recognizer::Candidate::ResetState()
    {
        /*
         * samples/linear位于PSRAM大对象中，不需要为每次候选整块清零；head/count决定有效区间，
         * Extract会覆盖linear的全部有效项。避免构造一个约6KiB临时Candidate压入主任务栈。
         */
        integrator = SysActionFrame::Integrator();
        head = 0;
        count = 0;
        ready = false;
        collecting = false;
        waiting_valley = false;
        candidate_id = 0;
        trigger_us = 0;
        settle_since_us = 0;
        quality_flags = QualityNone;
        accel_peak_g = 0.0f;
        trigger_gravity = {};
    }

    void Recognizer::ResetQuietEvidence()
    {
        quiet_since_us_ = 0;
        quiet_accel_sum_ = {};
        quiet_gyro_sum_ = {};
        quiet_count_ = 0;
    }

    void Recognizer::ResetPeakDetector()
    {
        peak_energy_ = 0.0f;
        valley_ready_ = false;
        valley_since_us_ = 0;
        last_trigger_us_ = 0;
    }

    void Recognizer::Reset()
    {
        candidates_[0].ResetState();
        candidates_[1].ResetState();
        ResetQuietEvidence();
        ResetPeakDetector();
        last_quiet_anchor_us_ = 0;
        last_input_us_ = 0;
        event_head_ = 0;
        event_count_ = 0;
        outcome_head_ = 0;
        outcome_count_ = 0;
        for (auto &event : events_)
            event = {};
        for (auto &outcome : outcomes_)
            outcome = {};
        if (entry_calibration_active_)
        {
            entry_calibration_complete_ = false;
            entry_calibration_started_us_ = 0;
        }
    }

    void Recognizer::BeginEntryCalibration()
    {
        entry_calibration_active_ = true;
        entry_calibration_complete_ = false;
        entry_calibration_started_us_ = 0;
        Reset();
    }

    bool Recognizer::IsEntryCalibrationComplete() const
    {
        return entry_calibration_active_ && entry_calibration_complete_;
    }

    void Recognizer::CancelEntryCalibration()
    {
        entry_calibration_active_ = false;
        entry_calibration_complete_ = false;
        entry_calibration_started_us_ = 0;
    }

    void Recognizer::UpdatePeakDetector(float energy, uint32_t timestamp_us)
    {
        if (energy > peak_energy_)
        {
            peak_energy_ = energy;
            valley_ready_ = false;
            valley_since_us_ = 0;
        }
        else if (peak_energy_ >= 1.0f &&
                 energy <= fmaxf(VALLEY_FLOOR, peak_energy_ * VALLEY_RATIO))
        {
            if (valley_since_us_ == 0)
                valley_since_us_ = timestamp_us;
            valley_ready_ = timestamp_us - valley_since_us_ >= VALLEY_HOLD_US;
        }
        else
        {
            valley_since_us_ = 0;
        }
    }

    bool Recognizer::UpdateCandidate(Candidate &candidate,
                                     const InputSample &sample,
                                     uint16_t frame_quality)
    {
        if (!candidate.ready)
            return true;

        SysActionFrame::FrameSample frame = {};
        if (!candidate.integrator.Update(sample.timestamp_us, sample.body_imu, &frame))
            return false;

        uint8_t target = 0;
        if (candidate.count < RING_CAPACITY)
        {
            target = static_cast<uint8_t>((candidate.head + candidate.count) % RING_CAPACITY);
            ++candidate.count;
        }
        else
        {
            target = candidate.head;
            candidate.head = static_cast<uint8_t>((candidate.head + 1) % RING_CAPACITY);
        }
        candidate.samples[target] = frame;
        Candidate::HumanFrameSample &human = candidate.human[target];
        human.timestamp_us = sample.timestamp_us;
        human.valid = sample.human_frame_valid;
        human.heading_stabilized = sample.human_frame_valid && sample.human_heading_stabilized;
        human.linear_accel_human_g = sample.human_linear_accel_g;
        human.gyro_human_dps = sample.human_gyro_dps;
        candidate.quality_flags = static_cast<uint16_t>(candidate.quality_flags | frame_quality);
        if (sample.accel_fresh)
            candidate.accel_peak_g = fmaxf(candidate.accel_peak_g, AccelMagnitude(sample.body_imu));
        return true;
    }

    bool Recognizer::ExtractHumanShadow(const Candidate &candidate,
                                        HumanShadowFeatures *features) const
    {
        if (!features || candidate.count == 0)
            return false;

        *features = {};
        uint16_t valid_count = 0;
        uint16_t heading_count = 0;
        uint16_t integrated_intervals = 0;
        bool previous_valid = false;
        Candidate::HumanFrameSample previous = {};
        Vector3 velocity = {};
        Vector3 peak_velocity = {};

        for (uint8_t index = 0; index < candidate.count; ++index)
        {
            const uint8_t ring_index = static_cast<uint8_t>(
                (candidate.head + index) % RING_CAPACITY);
            const Candidate::HumanFrameSample &current = candidate.human[ring_index];
            if (!current.valid)
            {
                /* 无效帧会打断梯形积分，不能跨过未知姿态或序号错配继续累计并伪造连续轨迹。 */
                previous_valid = false;
                continue;
            }

            ++valid_count;
            if (current.heading_stabilized)
                ++heading_count;
            features->linear_peak_g = fmaxf(
                features->linear_peak_g,
                SysActionFrame::Norm(current.linear_accel_human_g));

            if (previous_valid)
            {
                const uint32_t delta_us = current.timestamp_us - previous.timestamp_us;
                if (delta_us > 0 && delta_us <= 100000)
                {
                    const float half_dt = static_cast<float>(delta_us) * 0.5e-6f;
                    const Vector3 accel_step = {
                        (previous.linear_accel_human_g.x + current.linear_accel_human_g.x) * half_dt,
                        (previous.linear_accel_human_g.y + current.linear_accel_human_g.y) * half_dt,
                        (previous.linear_accel_human_g.z + current.linear_accel_human_g.z) * half_dt,
                    };
                    velocity.x += accel_step.x;
                    velocity.y += accel_step.y;
                    velocity.z += accel_step.z;
                    features->linear_impulse_x_gs += accel_step.x;
                    features->linear_impulse_y_gs += accel_step.y;
                    features->linear_impulse_z_gs += accel_step.z;
                    features->gyro_area_x_deg +=
                        (previous.gyro_human_dps.x + current.gyro_human_dps.x) * half_dt;
                    features->gyro_area_y_deg +=
                        (previous.gyro_human_dps.y + current.gyro_human_dps.y) * half_dt;
                    features->gyro_area_z_deg +=
                        (previous.gyro_human_dps.z + current.gyro_human_dps.z) * half_dt;
                    ++integrated_intervals;

                    const float speed = SysActionFrame::Norm(velocity);
                    if (speed > features->trajectory_peak_speed_gs)
                    {
                        features->trajectory_peak_speed_gs = speed;
                        peak_velocity = velocity;
                    }
                }
                else
                {
                    /* 超过100ms的真实断点不积分；后续可以从新的连续片段重新开始累计覆盖率。 */
                    velocity = {};
                }
            }
            previous = current;
            previous_valid = true;
        }

        features->coverage = static_cast<float>(valid_count) /
                             static_cast<float>(candidate.count);
        features->heading_coverage = static_cast<float>(heading_count) /
                                     static_cast<float>(candidate.count);
        if (features->trajectory_peak_speed_gs > 1.0e-6f)
        {
            const float inverse = 1.0f / features->trajectory_peak_speed_gs;
            features->trajectory_x = peak_velocity.x * inverse;
            features->trajectory_y = peak_velocity.y * inverse;
            features->trajectory_z = peak_velocity.z * inverse;
        }
        features->valid = integrated_intervals > 0;
        return features->valid;
    }

    bool Recognizer::AnchorPrimary(const InputSample &sample)
    {
        if (quiet_count_ < 3)
            return false;

        const float inverse = 1.0f / static_cast<float>(quiet_count_);
        const Vector3 gravity = SysActionFrame::Unit({quiet_accel_sum_.x * inverse,
                                                      quiet_accel_sum_.y * inverse,
                                                      quiet_accel_sum_.z * inverse});
        const Vector3 bias = {quiet_gyro_sum_.x * inverse,
                              quiet_gyro_sum_.y * inverse,
                              quiet_gyro_sum_.z * inverse};
        Candidate &primary = candidates_[0];
        primary.ResetState();
        candidates_[1].ResetState();
        if (!primary.integrator.Reset(gravity, bias))
            return false;
        primary.ready = true;
        primary.trigger_gravity = gravity;
        last_quiet_anchor_us_ = sample.timestamp_us;
        ResetPeakDetector();
        return UpdateCandidate(primary, sample, sample.quality_flags);
    }

    bool Recognizer::TryAnchorFromEntryQuiet(const InputSample &sample)
    {
        if (quiet_count_ < 3)
            return false;
        const float inverse = 1.0f / static_cast<float>(quiet_count_);
        const Vector3 gravity = SysActionFrame::Unit({quiet_accel_sum_.x * inverse,
                                                      quiet_accel_sum_.y * inverse,
                                                      quiet_accel_sum_.z * inverse});
        if (entry_calibration_active_ && !entry_calibration_complete_ &&
            (sample.timestamp_us - entry_calibration_started_us_ < ENTRY_CALIBRATION_MIN_US ||
             !EntryGravityMatchesScreenUp(gravity)))
        {
            return false;
        }
        if (!AnchorPrimary(sample))
            return false;
        if (entry_calibration_active_)
            entry_calibration_complete_ = true;
        return true;
    }

    void Recognizer::StartCollecting(Candidate &candidate, uint32_t timestamp_us)
    {
        candidate.collecting = true;
        candidate.waiting_valley = false;
        candidate.trigger_us = timestamp_us;
        candidate.candidate_id = next_candidate_id_++;
        if (next_candidate_id_ == 0)
            next_candidate_id_ = 1;
        candidate.settle_since_us = 0;
        TrimPreTriggerFrames(candidate);
    }

    void Recognizer::TrimPreTriggerFrames(Candidate &candidate)
    {
        if (candidate.count == 0)
            return;

        /*
         * micros()允许32位回绕，因此仍以有符号差比较时间；设备刚启动不足100ms时使用0作为
         * 饱和下界，避免trigger_us-PRE_TRIGGER_US在无符号运算中绕到未来并误删全部预滚动帧。
         */
        const uint32_t earliest = candidate.trigger_us >= PRE_TRIGGER_US
                                      ? candidate.trigger_us - PRE_TRIGGER_US
                                      : 0;
        while (candidate.count > 1 &&
               static_cast<int32_t>(candidate.samples[candidate.head].timestamp_us - earliest) < 0)
        {
            candidate.head = static_cast<uint8_t>((candidate.head + 1) % RING_CAPACITY);
            --candidate.count;
        }
    }

    bool Recognizer::RebasePrimary(const InputSample &sample,
                                   bool collect_now,
                                   uint16_t frame_quality)
    {
        Candidate &primary = candidates_[0];
        Vector3 gravity = {};
        Vector3 bias = {};
        if (!primary.integrator.SnapshotReference(&gravity, &bias))
            return false;

        primary.integrator = SysActionFrame::Integrator();
        if (!primary.integrator.Reset(gravity, bias))
            return false;
        primary.head = 0;
        primary.count = 0;
        primary.ready = true;
        primary.collecting = false;
        primary.waiting_valley = !collect_now;
        primary.candidate_id = 0;
        primary.trigger_us = 0;
        primary.settle_since_us = 0;
        primary.quality_flags = QualityNone;
        primary.accel_peak_g = 0.0f;
        primary.trigger_gravity = gravity;
        if (!UpdateCandidate(primary, sample, frame_quality))
            return false;
        if (collect_now)
            StartCollecting(primary, sample.timestamp_us);
        return true;
    }

    bool Recognizer::StartSecondary(const InputSample &sample, uint16_t frame_quality)
    {
        Candidate &primary = candidates_[0];
        Candidate &secondary = candidates_[1];
        if (secondary.collecting || !primary.ready ||
            (last_trigger_us_ != 0 &&
             sample.timestamp_us - last_trigger_us_ < MIN_CANDIDATE_INTERVAL_US))
        {
            return false;
        }

        Vector3 gravity = {};
        Vector3 bias = {};
        if (!primary.integrator.SnapshotReference(&gravity, &bias))
            return false;
        secondary.ResetState();
        if (!secondary.integrator.Reset(gravity, bias))
            return false;
        secondary.ready = true;
        secondary.trigger_gravity = gravity;
        if (!UpdateCandidate(secondary, sample, frame_quality))
            return false;
        StartCollecting(secondary, sample.timestamp_us);
        last_trigger_us_ = sample.timestamp_us;
        peak_energy_ = 0.0f;
        valley_ready_ = false;
        valley_since_us_ = 0;
        return true;
    }

    bool Recognizer::Extract(Candidate &candidate, Features *features)
    {
        if (!features || !candidate.ready || candidate.count == 0 ||
            SysActionFrame::Norm(candidate.trigger_gravity) < 0.9f)
        {
            return false;
        }

        for (uint8_t index = 0; index < candidate.count; ++index)
        {
            candidate.linear[index] =
                candidate.samples[static_cast<uint8_t>((candidate.head + index) % RING_CAPACITY)];
        }

        SysActionFrame::PhaseFeatures phase = {};
        if (!SysActionFrame::ExtractPhase(candidate.linear,
                                          candidate.count,
                                          candidate.trigger_us,
                                          &phase))
        {
            return false;
        }

        const Vector3 trajectory_direction =
            SysActionFrame::Unit(phase.trajectory_peak_velocity_local_gs);
        if (SysActionFrame::Norm(trajectory_direction) < 0.9f)
            return false;

        *features = {};
        features->gyro_peak_dps = phase.gyro_peak_dps;
        features->accel_peak_g = candidate.accel_peak_g;
        features->integrated_x = phase.main_axis_local.x;
        features->integrated_y = phase.main_axis_local.y;
        features->integrated_z = phase.main_axis_local.z;
        features->gravity_parallel =
            SysActionFrame::Dot(phase.main_axis_local, candidate.trigger_gravity);
        features->gravity_alignment = fabsf(features->gravity_parallel);
        features->main_axis_longitudinal = phase.main_axis_local.y;
        features->main_axis_xz_product = phase.main_axis_local.x * phase.main_axis_local.z;
        features->trajectory_peak_speed_gs = phase.trajectory_peak_speed_gs;
        features->trajectory_x = trajectory_direction.x;
        features->trajectory_y = trajectory_direction.y;
        features->trajectory_z = trajectory_direction.z;
        features->trajectory_vertical_alignment =
            SysActionFrame::Dot(trajectory_direction, candidate.trigger_gravity);
        features->trajectory_longitudinal = trajectory_direction.y;
        features->trajectory_xz_product = trajectory_direction.x * trajectory_direction.z;
        features->primary_offset_ms = phase.primary_offset_ms;
        features->primary_peak_dps = phase.primary_peak_dps;
        features->return_peak_dps = phase.return_peak_dps;
        features->primary_area_deg = phase.primary_area_deg;
        features->return_area_deg = phase.return_area_deg;
        features->return_to_primary_ratio = phase.return_to_primary_ratio;
        features->main_duration_ms = phase.main_duration_ms;
        features->return_delay_ms = phase.return_delay_ms;
        features->max_main_relative_angle_deg = phase.max_main_relative_angle_deg;
        features->end_main_relative_angle_deg = phase.end_main_relative_angle_deg;
        features->linear_accel_peak_g = phase.linear_accel_peak_g;
        features->linear_peak_x_g = phase.linear_accel_peak_local_g.x;
        features->linear_peak_y_g = phase.linear_accel_peak_local_g.y;
        features->linear_peak_z_g = phase.linear_accel_peak_local_g.z;
        features->linear_impulse_x_gs = phase.linear_impulse_local_gs.x;
        features->linear_impulse_y_gs = phase.linear_impulse_local_gs.y;
        features->linear_impulse_z_gs = phase.linear_impulse_local_gs.z;
        return true;
    }

    Classification Recognizer::ClassifyFeatures(const Features &features)
    {
        Classification result = {};

        const bool thrust_shape =
            features.primary_area_deg <= THRUST_MAX_PRIMARY_AREA_DEG &&
            features.main_duration_ms <= THRUST_MAX_PHASE_MS &&
            features.linear_accel_peak_g >= THRUST_MIN_LINEAR_ACCEL_G &&
            features.trajectory_peak_speed_gs >= THRUST_MIN_TRAJECTORY_SPEED_GS &&
            features.primary_peak_dps <= THRUST_MAX_PRIMARY_DPS &&
            features.max_main_relative_angle_deg <= THRUST_MAX_POSE_ANGLE_DEG;
        if (thrust_shape)
        {
            if (features.gravity_parallel <= THRUST_MAX_GRAVITY_PARALLEL &&
                features.main_axis_longitudinal >= THRUST_MIN_LONGITUDINAL_AXIS)
            {
                const float evidence = Min4(
                    ScoreBelow(features.primary_area_deg, THRUST_MAX_PRIMARY_AREA_DEG, 45.0f),
                    ScoreAbove(features.linear_accel_peak_g, THRUST_MIN_LINEAR_ACCEL_G, 2.0f),
                    ScoreAbove(features.trajectory_peak_speed_gs,
                               THRUST_MIN_TRAJECTORY_SPEED_GS, 0.20f),
                    ScoreBelow(features.max_main_relative_angle_deg,
                               THRUST_MAX_POSE_ANGLE_DEG, 45.0f));
                const float separation = Min2(
                    ScoreBelow(features.gravity_parallel, THRUST_MAX_GRAVITY_PARALLEL, 0.30f),
                    ScoreAbove(features.main_axis_longitudinal,
                               THRUST_MIN_LONGITUDINAL_AXIS, 0.35f));
                result.gesture = Gesture::Thrust;
                result.direction = 1;
                result.margin = separation;
                result.confidence = AcceptedConfidence(evidence, separation);
                result.reason = RejectReason::Accepted;
                return result;
            }
            result.reason = RejectReason::ThrustDirection;
            return result;
        }

        const bool trajectory_or_impact =
            features.trajectory_peak_speed_gs >= CUT_MIN_TRAJECTORY_SPEED_GS ||
            features.linear_accel_peak_g >= CUT_STRONG_LINEAR_ACCEL_G;
        const bool complete_cut =
            features.primary_peak_dps >= CUT_MIN_PRIMARY_PEAK_DPS &&
            features.max_main_relative_angle_deg >= CUT_MIN_POSE_ANGLE_DEG &&
            features.primary_area_deg <= CUT_MAX_PRIMARY_AREA_DEG &&
            features.linear_accel_peak_g >= CUT_MIN_LINEAR_ACCEL_G &&
            trajectory_or_impact &&
            features.return_to_primary_ratio <= CUT_MAX_RETURN_RATIO;
        if (!complete_cut)
        {
            result.reason = RejectReason::CutIncomplete;
            return result;
        }

        const float trajectory_score = fmaxf(
            ScoreAbove(features.trajectory_peak_speed_gs,
                       CUT_MIN_TRAJECTORY_SPEED_GS, 0.35f),
            ScoreAbove(features.linear_accel_peak_g, CUT_STRONG_LINEAR_ACCEL_G, 4.0f));
        const float cut_evidence = fminf(
            Min4(ScoreAbove(features.primary_peak_dps, CUT_MIN_PRIMARY_PEAK_DPS, 500.0f),
                 ScoreAbove(features.max_main_relative_angle_deg, CUT_MIN_POSE_ANGLE_DEG, 70.0f),
                 ScoreBelow(features.primary_area_deg, CUT_MAX_PRIMARY_AREA_DEG, 240.0f),
                 ScoreAbove(features.linear_accel_peak_g, CUT_MIN_LINEAR_ACCEL_G, 3.0f)),
            fminf(trajectory_score,
                  ScoreBelow(features.return_to_primary_ratio, CUT_MAX_RETURN_RATIO, 0.55f)));

        const float vertical = features.trajectory_vertical_alignment;
        if (vertical >= UPPERCUT_MIN_VERTICAL_ALIGNMENT)
        {
            const float longitudinal_accel_ratio =
                features.linear_accel_peak_g > 0.000001f
                    ? features.linear_peak_y_g / features.linear_accel_peak_g
                    : 0.0f;
            const bool uppercut_structure =
                fabsf(features.gravity_parallel) <= UPPERCUT_MAX_ABS_GRAVITY_PARALLEL &&
                fabsf(features.main_axis_longitudinal) <= UPPERCUT_MAX_ABS_LONGITUDINAL_AXIS &&
                longitudinal_accel_ratio >= UPPERCUT_MIN_LONGITUDINAL_ACCEL_RATIO;
            if (!uppercut_structure)
            {
                result.reason = RejectReason::UppercutStructure;
                return result;
            }
            const float separation = Min4(
                ScoreAbove(vertical, UPPERCUT_MIN_VERTICAL_ALIGNMENT, 0.45f),
                ScoreBelow(fabsf(features.gravity_parallel),
                           UPPERCUT_MAX_ABS_GRAVITY_PARALLEL, 0.30f),
                ScoreBelow(fabsf(features.main_axis_longitudinal),
                           UPPERCUT_MAX_ABS_LONGITUDINAL_AXIS, 0.25f),
                ScoreAbove(longitudinal_accel_ratio,
                           UPPERCUT_MIN_LONGITUDINAL_ACCEL_RATIO, 0.40f));
            result.gesture = Gesture::Uppercut;
            result.direction = -1;
            result.margin = separation;
            result.confidence = AcceptedConfidence(cut_evidence, separation);
            result.reason = RejectReason::Accepted;
            return result;
        }

        if (vertical >= HORIZONTAL_MIN_VERTICAL_ALIGNMENT)
        {
            const float lower_distance = vertical - HORIZONTAL_MIN_VERTICAL_ALIGNMENT;
            const float upper_distance = UPPERCUT_MIN_VERTICAL_ALIGNMENT - vertical;
            const float separation = Clamp01(fminf(lower_distance, upper_distance) / 0.30f);
            result.gesture = Gesture::HorizontalSlash;
            result.direction = features.gravity_parallel >= 0.0f ? 1 : -1;
            result.margin = separation;
            result.confidence = AcceptedConfidence(cut_evidence, separation);
            result.reason = RejectReason::Accepted;
            return result;
        }

        if (vertical <= VERTICAL_MAX_VERTICAL_ALIGNMENT)
        {
            const float separation = ScoreBelow(vertical, VERTICAL_MAX_VERTICAL_ALIGNMENT, 0.12f);
            result.gesture = Gesture::VerticalSlash;
            result.direction = 1;
            result.margin = separation;
            result.confidence = AcceptedConfidence(cut_evidence, separation);
            result.reason = RejectReason::Accepted;
            return result;
        }

        const float sector_distance = fminf(vertical - VERTICAL_MAX_VERTICAL_ALIGNMENT,
                                            HORIZONTAL_MIN_VERTICAL_ALIGNMENT - vertical);
        const float side_distance = Clamp01(fabsf(features.trajectory_xz_product) / 0.25f);
        const float separation = fminf(Clamp01(sector_distance / 0.16f), side_distance);
        result.gesture = features.trajectory_xz_product < 0.0f
                             ? Gesture::DiagonalSlashA
                             : Gesture::DiagonalSlashB;
        result.direction = 1;
        result.margin = separation;
        result.confidence = AcceptedConfidence(cut_evidence, separation);
        result.reason = RejectReason::Accepted;
        return result;
    }

    void Recognizer::QueueEvent(const GestureResult &event)
    {
        if (event.gesture == Gesture::None)
            return;
        if (event_count_ >= EVENT_CAPACITY)
        {
            event_head_ = static_cast<uint8_t>((event_head_ + 1) % EVENT_CAPACITY);
            --event_count_;
        }
        const uint8_t target = static_cast<uint8_t>((event_head_ + event_count_) % EVENT_CAPACITY);
        events_[target] = event;
        ++event_count_;
    }

    bool Recognizer::PopEvent(GestureResult *event)
    {
        if (!event || event_count_ == 0)
            return false;
        *event = events_[event_head_];
        event_head_ = static_cast<uint8_t>((event_head_ + 1) % EVENT_CAPACITY);
        --event_count_;
        return true;
    }

    void Recognizer::QueueOutcome(const CandidateOutcome &outcome)
    {
        if (outcome_count_ >= OUTCOME_CAPACITY)
        {
            outcome_head_ = static_cast<uint8_t>((outcome_head_ + 1) % OUTCOME_CAPACITY);
            --outcome_count_;
        }
        const uint8_t target = static_cast<uint8_t>((outcome_head_ + outcome_count_) % OUTCOME_CAPACITY);
        outcomes_[target] = outcome;
        ++outcome_count_;
    }

    bool Recognizer::PopCandidateOutcome(CandidateOutcome *outcome)
    {
        if (!outcome || outcome_count_ == 0)
            return false;
        *outcome = outcomes_[outcome_head_];
        outcome_head_ = static_cast<uint8_t>((outcome_head_ + 1) % OUTCOME_CAPACITY);
        --outcome_count_;
        return true;
    }

    bool Recognizer::ShouldAdaptiveFinish(Candidate &candidate, uint32_t now_us)
    {
        if (!candidate.collecting || candidate.settle_since_us == 0 ||
            now_us - candidate.trigger_us < ADAPTIVE_MIN_POST_US ||
            now_us - candidate.settle_since_us < ADAPTIVE_SETTLE_HOLD_US)
        {
            return false;
        }
        Features features = {};
        if (!Extract(candidate, &features))
            return false;
        const Classification classification = ClassifyFeatures(features);
        return classification.gesture != Gesture::None &&
               classification.confidence >= ADAPTIVE_MIN_CONFIDENCE;
    }

    void Recognizer::FinishCandidate(Candidate &candidate,
                                     bool is_primary,
                                     const InputSample &sample,
                                     bool adaptive_finished)
    {
        CandidateOutcome outcome = {};
        outcome.candidate_id = candidate.candidate_id;
        outcome.trigger_us = candidate.trigger_us;
        outcome.finished_us = sample.timestamp_us;
        outcome.quality_flags = candidate.quality_flags;
        outcome.adaptive_finished = adaptive_finished;
        outcome.features_valid = Extract(candidate, &outcome.features);
        /* 影子提取无论成功与否都不会改变features_valid、分类、收窗或事件队列。 */
        (void)ExtractHumanShadow(candidate, &outcome.human_shadow);
        outcome.classification = outcome.features_valid
                                     ? ClassifyFeatures(outcome.features)
                                     : Classification();
        QueueOutcome(outcome);

        const bool accepted = outcome.classification.gesture != Gesture::None;
        if (accepted)
        {
            GestureResult event = {};
            event.gesture = outcome.classification.gesture;
            event.timestamp_us = outcome.trigger_us;
            event.candidate_id = outcome.candidate_id;
            event.recognition_latency_us = sample.timestamp_us - outcome.trigger_us;
            event.strength_dps = outcome.features.gyro_peak_dps;
            event.confidence = outcome.classification.confidence;
            event.class_margin = outcome.classification.margin;
            event.direction = outcome.classification.direction;
            event.quality_flags = outcome.quality_flags;
            QueueEvent(event);
        }

        if (is_primary)
        {
            candidate.collecting = false;
            if (accepted && last_quiet_anchor_us_ != 0 &&
                sample.timestamp_us - last_quiet_anchor_us_ <= MAX_CHAIN_US)
            {
                if (!RebasePrimary(sample, false, sample.quality_flags))
                    candidate.ResetState();
            }
            else
            {
                candidate.ResetState();
            }
        }
        else
        {
            candidate.ResetState();
        }
    }

    bool Recognizer::Update(const InputSample &sample, GestureResult *out_event)
    {
        if (!out_event || !sample.gyro_fresh)
            return false;
        if (entry_calibration_active_ && entry_calibration_started_us_ == 0)
            entry_calibration_started_us_ = sample.timestamp_us;

        uint16_t frame_quality = sample.quality_flags;
        if (!sample.accel_fresh)
            frame_quality = static_cast<uint16_t>(frame_quality | QualityStaleAccel);
        if (last_input_us_ != 0)
        {
            const uint32_t gap_us = sample.timestamp_us - last_input_us_;
            if (gap_us > 30000 && gap_us <= 100000)
                frame_quality = static_cast<uint16_t>(frame_quality | QualityGapOver30Ms);
        }
        last_input_us_ = sample.timestamp_us;

        Candidate &primary = candidates_[0];
        Candidate &secondary = candidates_[1];
        if ((primary.ready && !UpdateCandidate(primary, sample, frame_quality)) ||
            (secondary.ready && !UpdateCandidate(secondary, sample, frame_quality)))
        {
            Reset();
            return false;
        }

        const float gyro = GyroMagnitude(sample.body_imu);
        const float accel_delta = fabsf(AccelMagnitude(sample.body_imu) - 1.0f);
        const bool quiet = gyro < QUIET_GYRO_DPS && accel_delta < QUIET_ACCEL_DELTA_G;
        const bool exceeds = gyro >= TRIGGER_GYRO_DPS || accel_delta >= TRIGGER_ACCEL_DELTA_G;
        const float energy = MotionEnergy(gyro, accel_delta);
        const bool valley_before = valley_ready_;
        UpdatePeakDetector(energy, sample.timestamp_us);

        Candidate *active_candidates[2] = {&primary, &secondary};
        for (Candidate *candidate : active_candidates)
        {
            if (!candidate->collecting)
                continue;
            if (energy <= ADAPTIVE_SETTLED_ENERGY)
            {
                if (candidate->settle_since_us == 0)
                    candidate->settle_since_us = sample.timestamp_us;
            }
            else
            {
                candidate->settle_since_us = 0;
            }
        }

        if (primary.ready && last_quiet_anchor_us_ != 0 &&
            sample.timestamp_us - last_quiet_anchor_us_ > MAX_CHAIN_US &&
            !primary.collecting && !secondary.collecting)
        {
            primary.ResetState();
            ResetPeakDetector();
        }

        if (quiet)
        {
            if (quiet_since_us_ == 0)
            {
                quiet_since_us_ = sample.timestamp_us;
                quiet_accel_sum_ = {};
                quiet_gyro_sum_ = {};
                quiet_count_ = 0;
            }
            if (sample.accel_fresh)
            {
                if (quiet_count_ >= QUIET_AVERAGE_SAMPLES)
                {
                    const float keep = static_cast<float>(QUIET_AVERAGE_SAMPLES - 1) /
                                       static_cast<float>(QUIET_AVERAGE_SAMPLES);
                    quiet_accel_sum_.x *= keep;
                    quiet_accel_sum_.y *= keep;
                    quiet_accel_sum_.z *= keep;
                    quiet_gyro_sum_.x *= keep;
                    quiet_gyro_sum_.y *= keep;
                    quiet_gyro_sum_.z *= keep;
                    quiet_count_ = QUIET_AVERAGE_SAMPLES - 1;
                }
                quiet_accel_sum_.x += sample.body_imu.axG;
                quiet_accel_sum_.y += sample.body_imu.ayG;
                quiet_accel_sum_.z += sample.body_imu.azG;
                quiet_gyro_sum_.x += sample.body_imu.gxDps;
                quiet_gyro_sum_.y += sample.body_imu.gyDps;
                quiet_gyro_sum_.z += sample.body_imu.gzDps;
                ++quiet_count_;
                if (!primary.collecting && !secondary.collecting &&
                    sample.timestamp_us - quiet_since_us_ >= QUIET_ARM_US)
                {
                    (void)TryAnchorFromEntryQuiet(sample);
                }
            }
        }
        else
        {
            ResetQuietEvidence();
            if (primary.collecting)
            {
                if (valley_before && exceeds && energy >= REARM_MIN_ENERGY)
                    (void)StartSecondary(sample, frame_quality);
            }
            else if (primary.waiting_valley && valley_before)
            {
                if (exceeds && energy >= REARM_MIN_ENERGY)
                {
                    if (sample.timestamp_us - last_trigger_us_ >= MIN_CANDIDATE_INTERVAL_US &&
                        RebasePrimary(sample, true, frame_quality))
                    {
                        primary.waiting_valley = false;
                        last_trigger_us_ = sample.timestamp_us;
                        peak_energy_ = energy;
                        valley_ready_ = false;
                    }
                }
                else if (RebasePrimary(sample, false, frame_quality))
                {
                    primary.waiting_valley = true;
                    peak_energy_ = 0.0f;
                    valley_ready_ = false;
                }
            }
            else if (primary.ready && !primary.waiting_valley && exceeds)
            {
                StartCollecting(primary, sample.timestamp_us);
                last_trigger_us_ = sample.timestamp_us;
                peak_energy_ = energy;
                valley_ready_ = false;
            }
        }

        const bool primary_adaptive = ShouldAdaptiveFinish(primary, sample.timestamp_us);
        const bool secondary_adaptive = ShouldAdaptiveFinish(secondary, sample.timestamp_us);
        if (primary.collecting &&
            (primary_adaptive ||
             DeadlineReached(sample.timestamp_us, primary.trigger_us + HARD_WINDOW_POST_US)))
        {
            FinishCandidate(primary, true, sample, primary_adaptive);
        }
        if (secondary.collecting &&
            (secondary_adaptive ||
             DeadlineReached(sample.timestamp_us, secondary.trigger_us + HARD_WINDOW_POST_US)))
        {
            FinishCandidate(secondary, false, sample, secondary_adaptive);
        }

        return PopEvent(out_event);
    }
}
