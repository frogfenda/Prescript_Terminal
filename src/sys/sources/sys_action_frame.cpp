/*
【模块职责】实现动作起手局部坐标和通用相位提取；数学合同与tools/imu/action_frame.py同步。
【重要约束】所有积分使用真实dt；超过100ms的未知区间明确失败，不钳位成伪造动作角度。
*/
#include "sys/sys_action_frame.h"

#include <math.h>

namespace SysActionFrame
{
    namespace
    {
        constexpr uint32_t MAX_GAP_US = 100000;
        constexpr float DEG_TO_RAD_F = 0.01745329252f;
        constexpr float RAD_TO_DEG_F = 57.29577951f;

        Vector3 Subtract(const Vector3 &left, const Vector3 &right)
        {
            return {left.x - right.x, left.y - right.y, left.z - right.z};
        }

        Vector3 Scale(const Vector3 &value, float factor)
        {
            return {value.x * factor, value.y * factor, value.z * factor};
        }

        Quaternion Normalize(const Quaternion &value)
        {
            const float length = sqrtf(value.w * value.w + value.x * value.x +
                                       value.y * value.y + value.z * value.z);
            if (!isfinite(length) || length <= 0.000000001f)
                return {};
            const float inverse = 1.0f / length;
            return {value.w * inverse, value.x * inverse, value.y * inverse, value.z * inverse};
        }

        Quaternion Conjugate(const Quaternion &value)
        {
            return {value.w, -value.x, -value.y, -value.z};
        }

        Quaternion Multiply(const Quaternion &left, const Quaternion &right)
        {
            return {
                left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
                left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
                left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
                left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
            };
        }

        Vector3 Rotate(const Quaternion &rotation, const Vector3 &value)
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

        Quaternion FromRotationVector(const Vector3 &rotation_radians)
        {
            const float angle = Norm(rotation_radians);
            if (angle <= 0.000000001f)
            {
                return Normalize({1.0f,
                                  rotation_radians.x * 0.5f,
                                  rotation_radians.y * 0.5f,
                                  rotation_radians.z * 0.5f});
            }
            const float half_angle = angle * 0.5f;
            const float factor = sinf(half_angle) / angle;
            return {cosf(half_angle),
                    rotation_radians.x * factor,
                    rotation_radians.y * factor,
                    rotation_radians.z * factor};
        }

        float QuaternionAngleDegrees(const Quaternion &value)
        {
            const Quaternion normalized = Normalize(value);
            const float absolute_w = fminf(1.0f, fmaxf(-1.0f, fabsf(normalized.w)));
            return 2.0f * acosf(absolute_w) * RAD_TO_DEG_F;
        }

        Vector3 TrapezoidVector(const FrameSample *samples,
                                uint8_t start,
                                uint8_t end,
                                bool gyro)
        {
            Vector3 total = {};
            for (uint8_t index = (uint8_t)(start + 1); index <= end; ++index)
            {
                const uint32_t delta_us = samples[index].timestamp_us - samples[index - 1].timestamp_us;
                if (delta_us == 0 || delta_us > MAX_GAP_US)
                    continue;
                const float half_dt = (float)delta_us * 0.0000005f;
                const Vector3 &previous = gyro ? samples[index - 1].gyro_local_dps
                                               : samples[index - 1].linear_accel_local_g;
                const Vector3 &current = gyro ? samples[index].gyro_local_dps
                                              : samples[index].linear_accel_local_g;
                total.x += (previous.x + current.x) * half_dt;
                total.y += (previous.y + current.y) * half_dt;
                total.z += (previous.z + current.z) * half_dt;
            }
            return total;
        }
    }

    float Norm(const Vector3 &value)
    {
        return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
    }

    Vector3 Unit(const Vector3 &value)
    {
        const float length = Norm(value);
        if (!isfinite(length) || length <= 0.000000001f)
            return {};
        return Scale(value, 1.0f / length);
    }

    float Dot(const Vector3 &left, const Vector3 &right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    bool Integrator::Reset(const Vector3 &anchor_gravity_body,
                           const Vector3 &gyro_bias_body_dps)
    {
        const float gravity_length = Norm(anchor_gravity_body);
        if (!isfinite(gravity_length) || gravity_length < 0.5f || gravity_length > 1.5f)
            return false;
        anchor_gravity_body_ = anchor_gravity_body;
        gyro_bias_body_dps_ = gyro_bias_body_dps;
        relative_orientation_ = {};
        last_timestamp_us_ = 0;
        has_timestamp_ = false;
        valid_ = true;
        return true;
    }

    bool Integrator::Update(uint32_t timestamp_us,
                            const SysPose::ImuSample &body_imu,
                            FrameSample *out)
    {
        if (!valid_ || !out)
            return false;

        float delta_seconds = 0.0f;
        if (has_timestamp_)
        {
            const uint32_t delta_us = timestamp_us - last_timestamp_us_;
            if (delta_us == 0 || delta_us > MAX_GAP_US)
            {
                valid_ = false;
                return false;
            }
            delta_seconds = (float)delta_us / 1000000.0f;
            const Vector3 unbiased_gyro = {
                body_imu.gxDps - gyro_bias_body_dps_.x,
                body_imu.gyDps - gyro_bias_body_dps_.y,
                body_imu.gzDps - gyro_bias_body_dps_.z,
            };
            const Vector3 rotation_radians = Scale(unbiased_gyro, delta_seconds * DEG_TO_RAD_F);
            relative_orientation_ = Normalize(Multiply(
                relative_orientation_, FromRotationVector(rotation_radians)));
        }
        last_timestamp_us_ = timestamp_us;
        has_timestamp_ = true;

        const Vector3 unbiased_gyro = {
            body_imu.gxDps - gyro_bias_body_dps_.x,
            body_imu.gyDps - gyro_bias_body_dps_.y,
            body_imu.gzDps - gyro_bias_body_dps_.z,
        };
        const Vector3 accel_body = {body_imu.axG, body_imu.ayG, body_imu.azG};
        const Vector3 gravity_body = Rotate(Conjugate(relative_orientation_), anchor_gravity_body_);
        const Vector3 linear_body = Subtract(accel_body, gravity_body);

        out->timestamp_us = timestamp_us;
        out->gyro_local_dps = Rotate(relative_orientation_, unbiased_gyro);
        out->linear_accel_local_g = Rotate(relative_orientation_, linear_body);
        out->relative_angle_deg = QuaternionAngleDegrees(relative_orientation_);
        return true;
    }

    bool ExtractPhase(const FrameSample *samples,
                      uint8_t sample_count,
                      uint32_t trigger_us,
                      PhaseFeatures *out)
    {
        if (!samples || !out || sample_count < 3)
            return false;

        int primary_index = -1;
        float search_peak = -1.0f;
        for (uint8_t index = 0; index < sample_count; ++index)
        {
            const int32_t offset_us = (int32_t)(samples[index].timestamp_us - trigger_us);
            if (offset_us < -30000 || offset_us > 350000)
                continue;
            const float magnitude = Norm(samples[index].gyro_local_dps);
            if (magnitude > search_peak)
            {
                search_peak = magnitude;
                primary_index = index;
            }
        }
        if (primary_index < 0)
            return false;

        uint8_t core_start = (uint8_t)primary_index;
        uint8_t core_end = (uint8_t)primary_index;
        while (core_start > 0 &&
               samples[primary_index].timestamp_us - samples[core_start - 1].timestamp_us <= 80000)
            --core_start;
        while (core_end + 1 < sample_count &&
               samples[core_end + 1].timestamp_us - samples[primary_index].timestamp_us <= 80000)
            ++core_end;

        Vector3 main_axis = Unit(TrapezoidVector(samples, core_start, core_end, true));
        if (Norm(main_axis) < 0.9f)
            return false;
        if (Dot(samples[primary_index].gyro_local_dps, main_axis) < 0.0f)
            main_axis = Scale(main_axis, -1.0f);

        float projection[MAX_PHASE_SAMPLES] = {};
        if (sample_count > MAX_PHASE_SAMPLES)
            return false;
        float primary_peak = 0.0f;
        float gyro_peak = 0.0f;
        for (uint8_t index = 0; index < sample_count; ++index)
        {
            projection[index] = Dot(samples[index].gyro_local_dps, main_axis);
            primary_peak = fmaxf(primary_peak, projection[index]);
            gyro_peak = fmaxf(gyro_peak, Norm(samples[index].gyro_local_dps));
        }
        if (primary_peak <= 0.0f)
            return false;

        const float primary_arm = fmaxf(120.0f, primary_peak * 0.18f);
        const float primary_release = primary_arm * 0.35f;
        const float return_arm = fmaxf(80.0f, primary_peak * 0.10f);
        uint8_t main_start = (uint8_t)primary_index;
        while (main_start > 0 && projection[main_start - 1] >= primary_release)
            --main_start;
        uint8_t main_end = (uint8_t)primary_index;
        while (main_end + 1 < sample_count)
        {
            ++main_end;
            if (projection[main_end] < primary_release)
                break;
        }

        int return_index = -1;
        float primary_area = 0.0f;
        float return_area = 0.0f;
        float return_peak = 0.0f;
        for (uint8_t index = 1; index < sample_count; ++index)
        {
            const uint32_t delta_us = samples[index].timestamp_us - samples[index - 1].timestamp_us;
            if (delta_us == 0 || delta_us > MAX_GAP_US)
                continue;
            const float half_dt = (float)delta_us * 0.0000005f;
            if (index > main_start && index <= main_end)
            {
                primary_area += (fmaxf(projection[index - 1], 0.0f) +
                                 fmaxf(projection[index], 0.0f)) *
                                half_dt;
            }
            if (index > main_end)
            {
                return_area += (fmaxf(-projection[index - 1], 0.0f) +
                                fmaxf(-projection[index], 0.0f)) *
                               half_dt;
                return_peak = fmaxf(return_peak, -projection[index]);
                if (return_index < 0 && projection[index] <= -return_arm)
                    return_index = index;
            }
        }

        uint8_t impulse_end = main_end;
        while (impulse_end + 1 < sample_count &&
               samples[impulse_end + 1].timestamp_us - samples[main_end].timestamp_us <= 50000)
            ++impulse_end;
        uint8_t linear_peak_index = main_start;
        float linear_peak = -1.0f;
        float max_main_angle = 0.0f;
        for (uint8_t index = main_start; index <= impulse_end; ++index)
        {
            const float magnitude = Norm(samples[index].linear_accel_local_g);
            if (magnitude > linear_peak)
            {
                linear_peak = magnitude;
                linear_peak_index = index;
            }
            if (index <= main_end)
                max_main_angle = fmaxf(max_main_angle, samples[index].relative_angle_deg);
        }

        /*
         * 单个加速度峰更像“敲击有多重”，不能直接代表横斩/竖斩的真实路径。这里从触发前
         * 30ms开始积分到触发后300ms，保留期间达到的最大速度向量。窗口短于完整回位，既能
         * 覆盖自然短行程的主移动，又尽量不让使用者把设备拿回起点时抵消主方向。
         */
        Vector3 trajectory_velocity = {};
        Vector3 trajectory_peak_velocity = {};
        float trajectory_peak_speed = 0.0f;
        for (uint8_t index = 1; index < sample_count; ++index)
        {
            const int32_t previous_offset =
                (int32_t)(samples[index - 1].timestamp_us - trigger_us);
            const int32_t current_offset =
                (int32_t)(samples[index].timestamp_us - trigger_us);
            if (previous_offset < -30000 || current_offset > 300000)
                continue;

            const uint32_t delta_us = samples[index].timestamp_us - samples[index - 1].timestamp_us;
            if (delta_us == 0 || delta_us > MAX_GAP_US)
                continue;
            const float half_dt = (float)delta_us * 0.0000005f;
            trajectory_velocity.x +=
                (samples[index - 1].linear_accel_local_g.x + samples[index].linear_accel_local_g.x) *
                half_dt;
            trajectory_velocity.y +=
                (samples[index - 1].linear_accel_local_g.y + samples[index].linear_accel_local_g.y) *
                half_dt;
            trajectory_velocity.z +=
                (samples[index - 1].linear_accel_local_g.z + samples[index].linear_accel_local_g.z) *
                half_dt;
            const float speed = Norm(trajectory_velocity);
            if (speed > trajectory_peak_speed)
            {
                trajectory_peak_speed = speed;
                trajectory_peak_velocity = trajectory_velocity;
            }
        }

        *out = {};
        out->primary_timestamp_us = samples[primary_index].timestamp_us;
        out->main_axis_local = main_axis;
        out->gyro_peak_dps = gyro_peak;
        out->primary_peak_dps = primary_peak;
        out->return_peak_dps = return_peak;
        out->primary_area_deg = primary_area;
        out->return_area_deg = return_area;
        out->return_to_primary_ratio = return_area / fmaxf(primary_area, 1.0f);
        out->primary_offset_ms = (float)((int32_t)(samples[primary_index].timestamp_us - trigger_us)) / 1000.0f;
        out->main_duration_ms = (float)(samples[main_end].timestamp_us - samples[main_start].timestamp_us) / 1000.0f;
        out->return_delay_ms = return_index >= 0
                                   ? (float)((int32_t)(samples[return_index].timestamp_us -
                                                      samples[primary_index].timestamp_us)) /
                                         1000.0f
                                   : -1.0f;
        out->max_main_relative_angle_deg = max_main_angle;
        out->end_main_relative_angle_deg = samples[main_end].relative_angle_deg;
        out->linear_accel_peak_g = linear_peak;
        out->linear_accel_peak_local_g = samples[linear_peak_index].linear_accel_local_g;
        out->linear_impulse_local_gs = TrapezoidVector(samples, main_start, impulse_end, false);
        out->trajectory_peak_speed_gs = trajectory_peak_speed;
        out->trajectory_peak_velocity_local_gs = trajectory_peak_velocity;
        return true;
    }
}
