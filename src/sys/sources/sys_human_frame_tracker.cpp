/*
【模块职责】实现固定入口人体坐标追踪。纯陀螺仪通道用于测量真实漂移；静止辅助通道只在
低角速度且加速度接近校准重力时使用加速度修正倾斜，避免把动作冲击误当成重力。
【统计语义】去重力残差只在 fresh 加速度帧累计 RMS；丢序、长间隔和 stale 加速度分别计数，
不能用平滑或时间钳位隐藏采样质量问题。
*/
#include "sys/sys_human_frame_tracker.h"

#include <math.h>

namespace SysHumanFrame
{
    namespace
    {
        constexpr float CALIBRATION_GYRO_DPS = 120.0f;
        constexpr float CALIBRATION_ACCEL_DELTA_G = 0.25f;
        constexpr uint32_t CALIBRATION_QUIET_US = 70000;
        constexpr uint32_t CALIBRATION_MIN_US = 1000000;
        constexpr uint32_t MAX_GAP_US = 100000;
        constexpr uint32_t QUALITY_GAP_US = 30000;

        // 动作期间禁用加速度反馈；只有更严格的近静止帧才允许纠正俯仰和横滚。
        constexpr float AIDED_GYRO_DPS = 80.0f;
        constexpr float AIDED_ACCEL_DELTA_G = 0.12f;
        constexpr float YAW_THRESHOLDS_DEG[4] = {1.0f, 3.0f, 5.0f, 10.0f};
        constexpr float TILT_THRESHOLDS_DEG[3] = {1.0f, 3.0f, 5.0f};

        float Norm(const Vector3 &value)
        {
            return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        float AccelMagnitude(const SysPose::ImuSample &imu)
        {
            return sqrtf(imu.axG * imu.axG + imu.ayG * imu.ayG + imu.azG * imu.azG);
        }

        float GyroMagnitude(const SysPose::ImuSample &imu)
        {
            return sqrtf(imu.gxDps * imu.gxDps + imu.gyDps * imu.gyDps + imu.gzDps * imu.gzDps);
        }

        /** 用 Mahony 的机身到入口四元数执行 q*v*q^-1，把机身向量旋转到入口人体坐标。 */
        Vector3 RotateBodyToHuman(const SysPose::Quaternion &rotation, const Vector3 &value)
        {
            const float tx = 2.0f * (rotation.y * value.z - rotation.z * value.y);
            const float ty = 2.0f * (rotation.z * value.x - rotation.x * value.z);
            const float tz = 2.0f * (rotation.x * value.y - rotation.y * value.x);
            return {
                value.x + rotation.w * tx + rotation.y * tz - rotation.z * ty,
                value.y + rotation.w * ty + rotation.z * tx - rotation.x * tz,
                value.z + rotation.w * tz + rotation.x * ty - rotation.y * tx,
            };
        }

        float SignedYaw(float yaw_deg)
        {
            return yaw_deg > 180.0f ? yaw_deg - 360.0f : yaw_deg;
        }

        float WrappedDelta(float current, float previous)
        {
            float delta = current - previous;
            while (delta > 180.0f)
                delta -= 360.0f;
            while (delta < -180.0f)
                delta += 360.0f;
            return delta;
        }

        bool EntryGravityMatchesScreenUp(const Vector3 &gravity)
        {
            const float magnitude = Norm(gravity);
            if (!isfinite(magnitude) || magnitude < 0.5f || magnitude > 1.5f)
                return false;
            const float inverse = 1.0f / magnitude;
            return gravity.z * inverse >= 0.70f &&
                   fabsf(gravity.x * inverse) <= 0.45f &&
                   fabsf(gravity.y * inverse) <= 0.45f;
        }
    }

    void Tracker::ResetQuietEvidence()
    {
        quiet_started_ = false;
        quiet_since_us_ = 0;
        accel_sum_ = {};
        gyro_sum_ = {};
        quiet_sample_count_ = 0;
        quiet_sample_write_ = 0;
    }

    void Tracker::AppendCalibrationSample(const InputSample &sample)
    {
        const Vector3 accel = {sample.body_imu.axG, sample.body_imu.ayG, sample.body_imu.azG};
        const Vector3 gyro = {sample.body_imu.gxDps, sample.body_imu.gyDps, sample.body_imu.gzDps};

        /*
         * 校准窗口达到 32 帧后先减去即将被覆盖的最旧样本，再加入新样本。这样入口重力和零偏
         * 始终来自真实的最近 32 帧，而不是把全程历史或近似缩放误写成“最近窗口”。
         */
        if (quiet_sample_count_ == CALIBRATION_SAMPLE_CAPACITY)
        {
            const Vector3 &old_accel = accel_samples_[quiet_sample_write_];
            const Vector3 &old_gyro = gyro_samples_[quiet_sample_write_];
            accel_sum_.x -= old_accel.x;
            accel_sum_.y -= old_accel.y;
            accel_sum_.z -= old_accel.z;
            gyro_sum_.x -= old_gyro.x;
            gyro_sum_.y -= old_gyro.y;
            gyro_sum_.z -= old_gyro.z;
        }
        else
        {
            ++quiet_sample_count_;
        }

        accel_samples_[quiet_sample_write_] = accel;
        gyro_samples_[quiet_sample_write_] = gyro;
        quiet_sample_write_ = (quiet_sample_write_ + 1) % CALIBRATION_SAMPLE_CAPACITY;
        accel_sum_.x += accel.x;
        accel_sum_.y += accel.y;
        accel_sum_.z += accel.z;
        gyro_sum_.x += gyro.x;
        gyro_sum_.y += gyro.y;
        gyro_sum_.z += gyro.z;
    }

    void Tracker::Begin()
    {
        gyro_solver_.Begin(104.0f);
        aided_solver_.Begin(104.0f);
        snapshot_ = {};
        snapshot_.status = Status::Calibrating;
        calibration_started_ = false;
        calibration_started_us_ = 0;
        last_sequence_ = 0;
        last_timestamp_us_ = 0;
        gravity_magnitude_g_ = 1.0f;
        initial_gyro_pitch_deg_ = 0.0f;
        initial_gyro_roll_deg_ = 0.0f;
        initial_aided_pitch_deg_ = 0.0f;
        initial_aided_roll_deg_ = 0.0f;
        previous_gyro_yaw_deg_ = 0.0f;
        previous_aided_yaw_deg_ = 0.0f;
        linear_residual_square_sum_ = 0.0;
        linear_residual_count_ = 0;
        ResetQuietEvidence();
    }

    bool Tracker::FinishCalibration(const InputSample &sample)
    {
        if (quiet_sample_count_ < 3)
            return false;
        const float inverse = 1.0f / static_cast<float>(quiet_sample_count_);
        const Vector3 gravity = {accel_sum_.x * inverse,
                                 accel_sum_.y * inverse,
                                 accel_sum_.z * inverse};
        if (!EntryGravityMatchesScreenUp(gravity))
            return false;

        SysPose::ImuSample anchor = {};
        anchor.axG = gravity.x;
        anchor.ayG = gravity.y;
        anchor.azG = gravity.z;
        if (!gyro_solver_.ResetFromAccel(anchor) || !aided_solver_.ResetFromAccel(anchor))
            return false;

        snapshot_.gyro_bias_dps = {gyro_sum_.x * inverse,
                                   gyro_sum_.y * inverse,
                                   gyro_sum_.z * inverse};
        gravity_magnitude_g_ = Norm(gravity);
        const SysPose::EulerAngles gyro_euler = gyro_solver_.GetResult(false).euler;
        const SysPose::EulerAngles aided_euler = aided_solver_.GetResult(false).euler;
        initial_gyro_pitch_deg_ = gyro_euler.pitchDeg;
        initial_gyro_roll_deg_ = gyro_euler.rollDeg;
        initial_aided_pitch_deg_ = aided_euler.pitchDeg;
        initial_aided_roll_deg_ = aided_euler.rollDeg;
        previous_gyro_yaw_deg_ = SignedYaw(gyro_euler.yawDeg);
        previous_aided_yaw_deg_ = SignedYaw(aided_euler.yawDeg);
        snapshot_.aided_orientation = aided_solver_.GetQuaternion();
        last_sequence_ = sample.sequence;
        last_timestamp_us_ = sample.timestamp_us;
        snapshot_.status = Status::Tracking;
        return true;
    }

    bool Tracker::UpdateCalibration(const InputSample &sample)
    {
        if (!calibration_started_)
        {
            calibration_started_ = true;
            calibration_started_us_ = sample.timestamp_us;
        }

        const float gyro = GyroMagnitude(sample.body_imu);
        const float accel_delta = fabsf(AccelMagnitude(sample.body_imu) - 1.0f);
        if (gyro >= CALIBRATION_GYRO_DPS || accel_delta >= CALIBRATION_ACCEL_DELTA_G)
        {
            ResetQuietEvidence();
            return false;
        }
        if (!quiet_started_)
        {
            quiet_started_ = true;
            quiet_since_us_ = sample.timestamp_us;
        }
        if (!sample.accel_fresh)
            return false;

        AppendCalibrationSample(sample);
        if (sample.timestamp_us - calibration_started_us_ < CALIBRATION_MIN_US ||
            sample.timestamp_us - quiet_since_us_ < CALIBRATION_QUIET_US)
        {
            return false;
        }
        return FinishCalibration(sample);
    }

    void Tracker::UpdateThresholdTimes()
    {
        const float yaw = fabsf(snapshot_.gyro_yaw_drift_deg);
        for (uint8_t index = 0; index < 4; ++index)
        {
            if (snapshot_.first_yaw_threshold_us[index] == 0 && yaw >= YAW_THRESHOLDS_DEG[index])
                snapshot_.first_yaw_threshold_us[index] = snapshot_.elapsed_us;
        }

        const float tilt = sqrtf(snapshot_.gyro_pitch_drift_deg * snapshot_.gyro_pitch_drift_deg +
                                 snapshot_.gyro_roll_drift_deg * snapshot_.gyro_roll_drift_deg);
        for (uint8_t index = 0; index < 3; ++index)
        {
            if (snapshot_.first_tilt_threshold_us[index] == 0 && tilt >= TILT_THRESHOLDS_DEG[index])
                snapshot_.first_tilt_threshold_us[index] = snapshot_.elapsed_us;
        }
    }

    bool Tracker::Update(const InputSample &sample)
    {
        if (!sample.gyro_fresh || snapshot_.status == Status::Discontinuous)
            return false;
        if (snapshot_.status == Status::Calibrating)
            return UpdateCalibration(sample);

        if (last_sequence_ != 0 && sample.sequence > last_sequence_ + 1)
            snapshot_.missed_sequences += sample.sequence - last_sequence_ - 1;
        last_sequence_ = sample.sequence;

        const uint32_t delta_us = sample.timestamp_us - last_timestamp_us_;
        last_timestamp_us_ = sample.timestamp_us;
        if (delta_us == 0 || delta_us > MAX_GAP_US)
        {
            ++snapshot_.discontinuities;
            snapshot_.status = Status::Discontinuous;
            return false;
        }
        if (delta_us > QUALITY_GAP_US)
            ++snapshot_.gaps_over_30ms;

        const float delta_seconds = static_cast<float>(delta_us) / 1000000.0f;
        snapshot_.elapsed_us += delta_us;
        ++snapshot_.processed_samples;
        if (!sample.accel_fresh)
            ++snapshot_.stale_accel_samples;

        SysPose::ImuSample corrected = sample.body_imu;
        corrected.gxDps -= snapshot_.gyro_bias_dps.x;
        corrected.gyDps -= snapshot_.gyro_bias_dps.y;
        corrected.gzDps -= snapshot_.gyro_bias_dps.z;

        SysPose::ImuSample gyro_only = corrected;
        gyro_only.axG = 0.0f;
        gyro_only.ayG = 0.0f;
        gyro_only.azG = 0.0f;
        if (!gyro_solver_.UpdateWithDeltaSeconds(gyro_only, delta_seconds))
        {
            ++snapshot_.discontinuities;
            snapshot_.status = Status::Discontinuous;
            return false;
        }

        const bool aided_quiet = sample.accel_fresh &&
                                 GyroMagnitude(corrected) < AIDED_GYRO_DPS &&
                                 fabsf(AccelMagnitude(corrected) - gravity_magnitude_g_) <
                                     AIDED_ACCEL_DELTA_G;
        SysPose::ImuSample aided = corrected;
        if (!aided_quiet)
        {
            aided.axG = 0.0f;
            aided.ayG = 0.0f;
            aided.azG = 0.0f;
        }
        if (!aided_solver_.UpdateWithDeltaSeconds(aided, delta_seconds))
        {
            ++snapshot_.discontinuities;
            snapshot_.status = Status::Discontinuous;
            return false;
        }

        const SysPose::EulerAngles gyro_euler = gyro_solver_.GetResult(false).euler;
        const SysPose::EulerAngles aided_euler = aided_solver_.GetResult(false).euler;
        const float gyro_yaw = SignedYaw(gyro_euler.yawDeg);
        const float aided_yaw = SignedYaw(aided_euler.yawDeg);
        snapshot_.gyro_yaw_drift_deg += WrappedDelta(gyro_yaw, previous_gyro_yaw_deg_);
        snapshot_.aided_yaw_drift_deg += WrappedDelta(aided_yaw, previous_aided_yaw_deg_);
        previous_gyro_yaw_deg_ = gyro_yaw;
        previous_aided_yaw_deg_ = aided_yaw;
        snapshot_.gyro_pitch_drift_deg = gyro_euler.pitchDeg - initial_gyro_pitch_deg_;
        snapshot_.gyro_roll_drift_deg = gyro_euler.rollDeg - initial_gyro_roll_deg_;
        snapshot_.aided_pitch_drift_deg = aided_euler.pitchDeg - initial_aided_pitch_deg_;
        snapshot_.aided_roll_drift_deg = aided_euler.rollDeg - initial_aided_roll_deg_;
        snapshot_.aided_orientation = aided_solver_.GetQuaternion();
        if (snapshot_.elapsed_us != 0)
        {
            snapshot_.yaw_drift_rate_deg_per_min = static_cast<float>(
                static_cast<double>(snapshot_.gyro_yaw_drift_deg) * 60000000.0 /
                static_cast<double>(snapshot_.elapsed_us));
        }

        if (sample.accel_fresh)
        {
            const Vector3 accel_body = {sample.body_imu.axG,
                                        sample.body_imu.ayG,
                                        sample.body_imu.azG};
            Vector3 aided_human = RotateBodyToHuman(aided_solver_.GetQuaternion(), accel_body);
            Vector3 gyro_human = RotateBodyToHuman(gyro_solver_.GetQuaternion(), accel_body);
            aided_human.z -= gravity_magnitude_g_;
            gyro_human.z -= gravity_magnitude_g_;
            snapshot_.linear_accel_human_g = aided_human;
            snapshot_.gyro_only_linear_accel_human_g = gyro_human;
            snapshot_.linear_residual_g = Norm(aided_human);
            snapshot_.linear_max_g = fmaxf(snapshot_.linear_max_g, snapshot_.linear_residual_g);
            linear_residual_square_sum_ += static_cast<double>(snapshot_.linear_residual_g) *
                                           static_cast<double>(snapshot_.linear_residual_g);
            ++linear_residual_count_;
            snapshot_.linear_rms_g = sqrtf(static_cast<float>(
                linear_residual_square_sum_ / static_cast<double>(linear_residual_count_)));
        }

        UpdateThresholdTimes();
        return true;
    }
}
