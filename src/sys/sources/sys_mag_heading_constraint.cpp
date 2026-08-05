/*
【实现说明】磁航向约束只使用校准后的Body磁场和人体追踪器当前四元数。
入口时保存水平磁场方向；后续把磁场旋到Human坐标后，以二维叉积/点积计算相对航向误差。
本阶段任何拒绝都只记录在Snapshot，绝不修改调用者的姿态或加速度结果。
*/
#include "sys/sys_mag_heading_constraint.h"

#include <math.h>

namespace SysMagHeading
{
    namespace
    {
        constexpr float RAD_TO_DEG_F = 57.2957795f;
    }

    void Constraint::Begin()
    {
        reference_horizontal_ = {};
        orientation_history_count_ = 0;
        orientation_history_write_ = 0;
        last_mag_sequence_ = 0;
        snapshot_ = {};
        snapshot_.reject_reason = RejectReason::NoReference;
    }

    void Constraint::PushOrientation(uint32_t timestamp_us,
                                     const SysPose::Quaternion &body_to_human)
    {
        SysPose::Quaternion normalized = body_to_human;
        if (!NormalizeQuaternion(&normalized))
            return;

        OrientationFrame &frame = orientation_history_[orientation_history_write_];
        frame.timestamp_us = timestamp_us;
        frame.orientation = normalized;
        orientation_history_write_ = static_cast<uint8_t>(
            (orientation_history_write_ + 1U) % ORIENTATION_HISTORY_CAPACITY);
        if (orientation_history_count_ < ORIENTATION_HISTORY_CAPACITY)
            ++orientation_history_count_;
    }

    SysMagVector3 Constraint::RotateBodyToHuman(const SysPose::Quaternion &rotation,
                                                 const SysMagVector3 &value)
    {
        /* 必须与SysHumanFrame追踪器的Body→Human四元数约定保持完全一致。 */
        const float tx = 2.0f * (rotation.y * value.z - rotation.z * value.y);
        const float ty = 2.0f * (rotation.z * value.x - rotation.x * value.z);
        const float tz = 2.0f * (rotation.x * value.y - rotation.y * value.x);
        return {
            value.x + rotation.w * tx + rotation.y * tz - rotation.z * ty,
            value.y + rotation.w * ty + rotation.z * tx - rotation.x * tz,
            value.z + rotation.w * tz + rotation.x * ty - rotation.y * tx,
        };
    }

    bool Constraint::IsFinite(const SysMagVector3 &value)
    {
        return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
    }

    bool Constraint::NormalizeQuaternion(SysPose::Quaternion *value)
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

    bool Constraint::InterpolateQuaternion(const SysPose::Quaternion &before,
                                           const SysPose::Quaternion &after,
                                           float fraction,
                                           SysPose::Quaternion *out)
    {
        if (!out || !isfinite(fraction))
            return false;
        fraction = fmaxf(0.0f, fminf(1.0f, fraction));

        /* q与-q代表同一旋转；先把后一帧翻到同一半球，再做归一化线性插值，
         * 避免跨符号边界时错误地绕四元数球面走长路。IMU相邻帧约10ms，
         * 这一插值误差远小于当前50Hz地磁链路的采样时间差。 */
        SysPose::Quaternion end = after;
        const float dot = before.w * after.w + before.x * after.x +
                          before.y * after.y + before.z * after.z;
        if (dot < 0.0f)
        {
            end.w = -end.w;
            end.x = -end.x;
            end.y = -end.y;
            end.z = -end.z;
        }
        const float before_weight = 1.0f - fraction;
        out->w = before.w * before_weight + end.w * fraction;
        out->x = before.x * before_weight + end.x * fraction;
        out->y = before.y * before_weight + end.y * fraction;
        out->z = before.z * before_weight + end.z * fraction;
        return NormalizeQuaternion(out);
    }

    bool Constraint::ResolveOrientation(uint32_t timestamp_us,
                                        SysPose::Quaternion *out,
                                        uint32_t *span_us) const
    {
        if (!out || !span_us || orientation_history_count_ == 0)
            return false;
        const uint8_t oldest = static_cast<uint8_t>(
            (orientation_history_write_ + ORIENTATION_HISTORY_CAPACITY -
             orientation_history_count_) % ORIENTATION_HISTORY_CAPACITY);

        for (uint8_t index = 0; index < orientation_history_count_; ++index)
        {
            const uint8_t slot = static_cast<uint8_t>(
                (oldest + index) % ORIENTATION_HISTORY_CAPACITY);
            const OrientationFrame &frame = orientation_history_[slot];
            if (frame.timestamp_us == timestamp_us)
            {
                *out = frame.orientation;
                *span_us = 0;
                return true;
            }
            if (index + 1U >= orientation_history_count_)
                break;

            const uint8_t next_slot = static_cast<uint8_t>(
                (oldest + index + 1U) % ORIENTATION_HISTORY_CAPACITY);
            const OrientationFrame &next = orientation_history_[next_slot];
            const int32_t from_before = static_cast<int32_t>(timestamp_us - frame.timestamp_us);
            const int32_t to_after = static_cast<int32_t>(next.timestamp_us - timestamp_us);
            if (from_before < 0 || to_after < 0)
                continue;

            const uint32_t frame_span = next.timestamp_us - frame.timestamp_us;
            if (frame_span == 0 || frame_span > MAX_ORIENTATION_GAP_US)
                return false;
            const float fraction = static_cast<float>(static_cast<uint32_t>(from_before)) /
                                   static_cast<float>(frame_span);
            if (!InterpolateQuaternion(frame.orientation, next.orientation, fraction, out))
                return false;
            *span_us = frame_span;
            return true;
        }
        return false;
    }

    bool Constraint::HorizontalDirection(const SysMagVector3 &human,
                                         SysMagVector3 *direction,
                                         float *magnitude)
    {
        if (!direction || !magnitude || !IsFinite(human))
            return false;
        const float horizontal = sqrtf(human.x * human.x + human.y * human.y);
        if (!isfinite(horizontal) || horizontal < MIN_HORIZONTAL_FIELD_UT)
            return false;
        const float inverse = 1.0f / horizontal;
        *direction = {human.x * inverse, human.y * inverse, 0.0f};
        *magnitude = horizontal;
        return true;
    }

    float Constraint::HeadingFromReference(const SysMagVector3 &reference,
                                           const SysMagVector3 &current)
    {
        const float dot = reference.x * current.x + reference.y * current.y;
        const float cross = reference.x * current.y - reference.y * current.x;
        return NormalizeSignedDeg(atan2f(cross, dot) * RAD_TO_DEG_F);
    }

    float Constraint::NormalizeSignedDeg(float value)
    {
        while (value > 180.0f)
            value -= 360.0f;
        while (value < -180.0f)
            value += 360.0f;
        return value;
    }

    bool Constraint::Update(uint32_t current_timestamp_us,
                            const SysPose::Quaternion &body_to_human,
                            const SysMagSample &mag)
    {
        if (mag.sequence == 0)
            return false;
        if (mag.sequence == last_mag_sequence_)
        {
            /* 没有新磁样本时仍刷新上一笔“完整结果”的年龄，便于调试页真实显示传感器掉线。
             * 等待时间对齐的新样本不会提前覆盖sample_timestamp_us，因此也不会把旧有效结果续命。 */
            if (snapshot_.sample_seen)
            {
                const int32_t signed_age = static_cast<int32_t>(
                    current_timestamp_us - snapshot_.sample_timestamp_us);
                snapshot_.sample_age_us = signed_age < 0 ? 0U : static_cast<uint32_t>(signed_age);
                if (signed_age < 0 || static_cast<uint32_t>(signed_age) > MAX_SAMPLE_AGE_US)
                {
                    snapshot_.accepted = false;
                    snapshot_.reject_reason = RejectReason::StaleSample;
                }
            }
            return false;
        }

        const int32_t signed_age = static_cast<int32_t>(current_timestamp_us - mag.timestamp_us);
        if (signed_age < 0 || static_cast<uint32_t>(signed_age) > MAX_SAMPLE_AGE_US)
        {
            last_mag_sequence_ = mag.sequence;
            snapshot_.sample_seen = true;
            snapshot_.accepted = false;
            snapshot_.time_aligned = false;
            snapshot_.sync_pending = false;
            snapshot_.confidence = mag.confidence;
            snapshot_.field_strength_uT = mag.field_strength_uT;
            snapshot_.disturbance_reasons = mag.disturbance_reasons;
            snapshot_.sample_timestamp_us = mag.timestamp_us;
            ++snapshot_.rejected_samples;
            snapshot_.sample_age_us = signed_age < 0 ? 0U : static_cast<uint32_t>(signed_age);
            snapshot_.orientation_span_us = 0;
            snapshot_.reject_reason = RejectReason::StaleSample;
            return true;
        }

        if (!mag.fusion_usable || mag.confidence < MIN_CONFIDENCE)
        {
            last_mag_sequence_ = mag.sequence;
            snapshot_.sample_seen = true;
            snapshot_.accepted = false;
            snapshot_.time_aligned = false;
            snapshot_.sync_pending = false;
            snapshot_.confidence = mag.confidence;
            snapshot_.field_strength_uT = mag.field_strength_uT;
            snapshot_.disturbance_reasons = mag.disturbance_reasons;
            snapshot_.sample_timestamp_us = mag.timestamp_us;
            snapshot_.sample_age_us = static_cast<uint32_t>(signed_age);
            snapshot_.orientation_span_us = 0;
            ++snapshot_.rejected_samples;
            snapshot_.reject_reason = RejectReason::NotUsable;
            return true;
        }

        SysPose::Quaternion aligned_orientation = {};
        uint32_t orientation_span_us = 0;
        if (!ResolveOrientation(mag.timestamp_us, &aligned_orientation, &orientation_span_us))
        {
            /* 地磁在SysMotion之后采样，第一次看到新磁样本时通常还缺少它后方的IMU帧。
             * 此处不消费序号、不累计拒绝，也不覆盖上一笔完整快照；只发布独立pending标记。
             * 这样UI与纠偏消费者不会在正常等待窗口里看到假F=0或假无效磁场。 */
            snapshot_.sync_pending = true;
            return false;
        }
        last_mag_sequence_ = mag.sequence;
        snapshot_.sample_seen = true;
        snapshot_.accepted = false;
        snapshot_.time_aligned = true;
        snapshot_.sync_pending = false;
        snapshot_.confidence = mag.confidence;
        snapshot_.field_strength_uT = mag.field_strength_uT;
        snapshot_.disturbance_reasons = mag.disturbance_reasons;
        snapshot_.sample_timestamp_us = mag.timestamp_us;
        snapshot_.sample_age_us = static_cast<uint32_t>(signed_age);
        snapshot_.orientation_span_us = orientation_span_us;
        snapshot_.reject_reason = RejectReason::None;

        const SysMagVector3 aligned_human = RotateBodyToHuman(aligned_orientation, mag.body_uT);
        const SysMagVector3 raw_human = RotateBodyToHuman(body_to_human, mag.body_uT);
        SysMagVector3 aligned_direction = {};
        SysMagVector3 raw_direction = {};
        float aligned_horizontal = 0.0f;
        float raw_horizontal = 0.0f;
        if (!HorizontalDirection(aligned_human, &aligned_direction, &aligned_horizontal))
        {
            ++snapshot_.rejected_samples;
            snapshot_.reject_reason = !IsFinite(aligned_human)
                                          ? RejectReason::InvalidVector
                                          : RejectReason::WeakHorizontalField;
            return true;
        }
        const bool raw_direction_valid = HorizontalDirection(raw_human,
                                                             &raw_direction,
                                                             &raw_horizontal);
        snapshot_.horizontal_field_uT = aligned_horizontal;

        if (!snapshot_.reference_valid)
        {
            reference_horizontal_ = aligned_direction;
            snapshot_.reference_valid = true;
            snapshot_.reference_horizontal_uT = aligned_horizontal;
            snapshot_.relative_heading_deg = 0.0f;
            snapshot_.raw_relative_heading_deg = 0.0f;
            snapshot_.alignment_delta_deg = 0.0f;
        }
        else
        {
            snapshot_.relative_heading_deg = HeadingFromReference(reference_horizontal_,
                                                                  aligned_direction);
            if (raw_direction_valid)
            {
                snapshot_.raw_relative_heading_deg = HeadingFromReference(reference_horizontal_,
                                                                          raw_direction);
                snapshot_.alignment_delta_deg = NormalizeSignedDeg(
                    snapshot_.relative_heading_deg - snapshot_.raw_relative_heading_deg);
            }
            else
            {
                /* 未对齐对照值不参与质量门；其水平投影退化时只标记为非数值，
                 * 不能反过来拒绝已经正确时间对齐的正式约束结果。 */
                snapshot_.raw_relative_heading_deg = NAN;
                snapshot_.alignment_delta_deg = NAN;
            }
        }

        snapshot_.accepted = true;
        snapshot_.reject_reason = RejectReason::None;
        ++snapshot_.accepted_samples;
        return true;
    }

    const char *Constraint::RejectReasonName(RejectReason reason)
    {
        switch (reason)
        {
        case RejectReason::None: return "OK";
        case RejectReason::NoReference: return "NO_REF";
        case RejectReason::StaleSample: return "STALE";
        case RejectReason::NotUsable: return "MAG_GATE";
        case RejectReason::OrientationUnavailable: return "SYNC_WAIT";
        case RejectReason::WeakHorizontalField: return "WEAK_H";
        case RejectReason::InvalidVector: return "INVALID";
        default: return "UNKNOWN";
        }
    }
}
