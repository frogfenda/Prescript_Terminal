/*
【实现说明】本模块不运行第二套IMU积分器。基础姿态仍由SysHumanFrame产生；这里只对确认可信、确认静止
后的磁航向误差做圆周低通，并用限速控制器维护Human Z轴修正角。最终四元数为qYaw*qBase，因此只改变
人体坐标中的航向，不会让地磁反馈参与pitch/roll或去重力加速度。
*/
#include "sys/sys_mag_aided_orientation.h"

#include <math.h>

namespace SysMagAidedOrientation
{
    namespace
    {
        constexpr float DEG_TO_RAD_F = 0.0174532925f;
    }

    void Tracker::Begin()
    {
        snapshot_ = {};
        snapshot_.orientation = {};
        snapshot_.state = State::WaitingHeading;
        last_update_us_ = 0;
        quiet_since_us_ = 0;
        last_heading_sample_count_ = 0;
        last_heading_timestamp_us_ = 0;
        filtered_heading_valid_ = false;
    }

    bool Tracker::NormalizeQuaternion(SysPose::Quaternion *value)
    {
        if (!value || !isfinite(value->w) || !isfinite(value->x) ||
            !isfinite(value->y) || !isfinite(value->z))
        {
            return false;
        }
        const float norm_square = value->w * value->w + value->x * value->x +
                                  value->y * value->y + value->z * value->z;
        if (!isfinite(norm_square) || norm_square < 1.0e-8f)
            return false;
        const float inverse_norm = 1.0f / sqrtf(norm_square);
        value->w *= inverse_norm;
        value->x *= inverse_norm;
        value->y *= inverse_norm;
        value->z *= inverse_norm;
        return true;
    }

    float Tracker::NormalizeSignedDeg(float value)
    {
        /* correction_deg允许耐久测试中连续累计多圈；这里用常数时间取模，避免运行数小时后
         * 每帧为了归一化做成百上千次while循环。 */
        float wrapped = fmodf(value + 180.0f, 360.0f);
        if (wrapped < 0.0f)
            wrapped += 360.0f;
        return wrapped - 180.0f;
    }

    float Tracker::WrappedDeltaDeg(float target, float current)
    {
        return NormalizeSignedDeg(target - current);
    }

    SysPose::Quaternion Tracker::ApplyHumanYaw(const SysPose::Quaternion &base,
                                                float correction_deg)
    {
        /* 左乘Human Z旋转：qOut=qYaw*qBase。若改成右乘，就会绕随设备倾斜的Body Z修正，
         * 从而把航向反馈耦合进pitch/roll；这里的乘法次序是该模块最重要的坐标约束。 */
        const float half_angle = 0.5f * NormalizeSignedDeg(correction_deg) * DEG_TO_RAD_F;
        const float yaw_w = cosf(half_angle);
        const float yaw_z = sinf(half_angle);
        SysPose::Quaternion out;
        out.w = yaw_w * base.w - yaw_z * base.z;
        out.x = yaw_w * base.x - yaw_z * base.y;
        out.y = yaw_w * base.y + yaw_z * base.x;
        out.z = yaw_w * base.z + yaw_z * base.w;
        NormalizeQuaternion(&out);
        return out;
    }

    void Tracker::UpdateHeadingFilter(const SysMagHeading::Snapshot &heading)
    {
        if (heading.accepted_samples == last_heading_sample_count_)
            return;
        last_heading_sample_count_ = heading.accepted_samples;

        if (!heading.accepted || !isfinite(heading.relative_heading_deg))
            return;

        const float input = NormalizeSignedDeg(heading.relative_heading_deg);
        if (!filtered_heading_valid_ || last_heading_timestamp_us_ == 0)
        {
            snapshot_.filtered_heading_error_deg = input;
            filtered_heading_valid_ = true;
        }
        else
        {
            const uint32_t delta_us = heading.sample_timestamp_us - last_heading_timestamp_us_;
            const float delta_s = static_cast<float>(delta_us) / 1000000.0f;
            const float alpha = (!isfinite(delta_s) || delta_s <= 0.0f || delta_s > 1.0f)
                                    ? 1.0f
                                    : delta_s / (HEADING_FILTER_TAU_S + delta_s);
            snapshot_.filtered_heading_error_deg = NormalizeSignedDeg(
                snapshot_.filtered_heading_error_deg +
                alpha * WrappedDeltaDeg(input, snapshot_.filtered_heading_error_deg));
        }
        last_heading_timestamp_us_ = heading.sample_timestamp_us;
        ++snapshot_.heading_samples_used;
    }

    bool Tracker::Update(uint32_t timestamp_us,
                         const SysPose::Quaternion &base_orientation,
                         float gyro_magnitude_dps,
                         float accel_delta_g,
                         const SysMagHeading::Snapshot &heading)
    {
        SysPose::Quaternion normalized_base = base_orientation;
        snapshot_.gyro_magnitude_dps = gyro_magnitude_dps;
        snapshot_.accel_delta_g = accel_delta_g;
        snapshot_.correction_active = false;

        if (!NormalizeQuaternion(&normalized_base) || !isfinite(gyro_magnitude_dps) ||
            !isfinite(accel_delta_g))
        {
            snapshot_.orientation_valid = false;
            snapshot_.state = State::InvalidOrientation;
            return false;
        }

        uint32_t delta_us = 0;
        if (last_update_us_ != 0)
            delta_us = timestamp_us - last_update_us_;
        last_update_us_ = timestamp_us;

        const bool continuous = delta_us > 0 && delta_us <= MAX_UPDATE_GAP_US;
        const bool quiet_now = continuous &&
                               gyro_magnitude_dps < QUIET_GYRO_MAX_DPS &&
                               accel_delta_g < QUIET_ACCEL_MAX_DELTA_G;
        if (!quiet_now)
        {
            quiet_since_us_ = 0;
            snapshot_.quiet_elapsed_ms = 0;
            snapshot_.quiet_ready = false;
            /* 运动期间出现的磁方向不进入低通。持续同步计数后，静止确认完成时只会接收新样本。 */
            last_heading_sample_count_ = heading.accepted_samples;
            snapshot_.state = State::Moving;
        }
        else
        {
            if (quiet_since_us_ == 0)
                quiet_since_us_ = timestamp_us;
            const uint32_t quiet_us = timestamp_us - quiet_since_us_;
            snapshot_.quiet_elapsed_ms = quiet_us / 1000U;
            snapshot_.quiet_ready = quiet_us >= QUIET_CONFIRM_US;
            if (!snapshot_.quiet_ready)
            {
                last_heading_sample_count_ = heading.accepted_samples;
                snapshot_.state = State::Settling;
            }
        }

        if (snapshot_.quiet_ready)
        {
            if (!heading.reference_valid)
            {
                snapshot_.state = State::WaitingHeading;
            }
            else if (!heading.accepted)
            {
                snapshot_.state = State::MagneticRejected;
            }
            else
            {
                UpdateHeadingFilter(heading);
                if (filtered_heading_valid_ && continuous)
                {
                    const float target_correction = -snapshot_.filtered_heading_error_deg;
                    const float remaining = WrappedDeltaDeg(target_correction,
                                                            snapshot_.correction_deg);
                    snapshot_.remaining_correction_deg = remaining;
                    if (fabsf(remaining) <= CORRECTION_DEADBAND_DEG)
                    {
                        snapshot_.state = State::Holding;
                    }
                    else
                    {
                        const float maximum_step = MAX_CORRECTION_RATE_DPS *
                                                   static_cast<float>(delta_us) / 1000000.0f;
                        const float step = fmaxf(-maximum_step, fminf(maximum_step, remaining));
                        /* 保留未包裹的累计修正供BASE/MAG耐久统计使用；四元数构造时才折回一圈。
                         * 否则跨过±180度时，数学姿态虽连续，屏幕MAG Y却会假跳360度。 */
                        snapshot_.correction_deg += step;
                        snapshot_.remaining_correction_deg = WrappedDeltaDeg(
                            target_correction, snapshot_.correction_deg);
                        snapshot_.correction_active = fabsf(step) > 0.0f;
                        snapshot_.state = State::Correcting;
                    }
                }
                else
                {
                    snapshot_.state = State::WaitingHeading;
                }
            }
        }

        snapshot_.orientation = ApplyHumanYaw(normalized_base, snapshot_.correction_deg);
        snapshot_.orientation_valid = true;
        return true;
    }

    const char *Tracker::StateName(State state)
    {
        switch (state)
        {
        case State::WaitingHeading: return "等待地磁";
        case State::Moving: return "运动冻结";
        case State::Settling: return "静止确认";
        case State::MagneticRejected: return "地磁拒绝";
        case State::Correcting: return "限速纠偏";
        case State::Holding: return "保持";
        case State::InvalidOrientation: return "姿态无效";
        default: return "未知";
        }
    }
}
