/*
【模块职责】把已经完成校准和机身轴映射的磁场转换为“相对入口人体坐标”的航向约束。
【输出边界】本模块只计算航向约束，不修改任何姿态四元数、不回写SysPose、不改变动作识别；
正式消费者由更上层的SysHumanMotion编排并决定如何使用约束。
【输入边界】调用者复制SysMag缓存和当前Body→Human四元数；本模块不访问I2C、文件或UI。
【时间语义】磁场样本必须带有SysMag自己的时间戳，调用者同时提供当前IMU时间戳，用于拒绝过期样本。
*/
#pragma once

#include <stdint.h>

#include "sys/sys_mag.h"
#include "sys/sys_pose_solver.h"

namespace SysMagHeading
{
    enum class RejectReason : uint8_t
    {
        None = 0,
        NoReference,
        StaleSample,
        NotUsable,
        OrientationUnavailable,
        WeakHorizontalField,
        InvalidVector,
    };

    struct Snapshot
    {
        bool reference_valid = false;
        bool sample_seen = false;
        bool accepted = false;
        bool time_aligned = false;
        // 新磁样本正在等待它后方的一帧IMU；此时其余字段仍代表上一笔完整结果，不能伪造F=0。
        bool sync_pending = false;
        float relative_heading_deg = 0.0f;
        float raw_relative_heading_deg = 0.0f;
        float alignment_delta_deg = 0.0f;
        float horizontal_field_uT = 0.0f;
        float reference_horizontal_uT = 0.0f;
        float field_strength_uT = 0.0f;
        float confidence = 0.0f;
        uint32_t sample_timestamp_us = 0;
        uint32_t sample_age_us = 0;
        uint32_t orientation_span_us = 0;
        uint32_t disturbance_reasons = SYS_MAG_DISTURBANCE_NOT_CALIBRATED |
                                       SYS_MAG_DISTURBANCE_AXIS_UNVERIFIED;
        uint32_t accepted_samples = 0;
        uint32_t rejected_samples = 0;
        RejectReason reject_reason = RejectReason::NoReference;
    };

    class Constraint
    {
    public:
        /** 清除入口参考方向和本轮统计；下一次有效姿态/磁场同时到达时重新建零。 */
        void Begin();

        /**
         * 保存一帧带IMU采样时间戳的Body->Human姿态。调用者应在Tracker处理每笔新IMU后按序调用；
         * 模块只保留约150ms固定历史，不分配内存，也不修改传入四元数。
         */
        void PushOrientation(uint32_t timestamp_us,
                             const SysPose::Quaternion &body_to_human);

        /**
         * 消费一份最新磁场并计算相对入口航向误差。
         * 返回true表示该磁场序号被消费；accepted由Snapshot单独表示质量门是否通过。
         */
        bool Update(uint32_t current_timestamp_us,
                    const SysPose::Quaternion &body_to_human,
                    const SysMagSample &mag);

        Snapshot GetSnapshot() const { return snapshot_; }

        static const char *RejectReasonName(RejectReason reason);

    private:
        static constexpr uint32_t MAX_SAMPLE_AGE_US = 100000;
        static constexpr uint32_t MAX_ORIENTATION_GAP_US = 30000;
        static constexpr float MIN_HORIZONTAL_FIELD_UT = 8.0f;
        static constexpr float MIN_CONFIDENCE = 0.75f;
        static constexpr uint8_t ORIENTATION_HISTORY_CAPACITY = 16;

        struct OrientationFrame
        {
            uint32_t timestamp_us = 0;
            SysPose::Quaternion orientation;
        };

        SysMagVector3 reference_horizontal_;
        OrientationFrame orientation_history_[ORIENTATION_HISTORY_CAPACITY] = {};
        uint8_t orientation_history_count_ = 0;
        uint8_t orientation_history_write_ = 0;
        uint32_t last_mag_sequence_ = 0;
        Snapshot snapshot_;

        static SysMagVector3 RotateBodyToHuman(const SysPose::Quaternion &rotation,
                                                const SysMagVector3 &value);
        static bool IsFinite(const SysMagVector3 &value);
        static bool NormalizeQuaternion(SysPose::Quaternion *value);
        static bool InterpolateQuaternion(const SysPose::Quaternion &before,
                                          const SysPose::Quaternion &after,
                                          float fraction,
                                          SysPose::Quaternion *out);
        bool ResolveOrientation(uint32_t timestamp_us,
                                SysPose::Quaternion *out,
                                uint32_t *span_us) const;
        static bool HorizontalDirection(const SysMagVector3 &human,
                                        SysMagVector3 *direction,
                                        float *magnitude);
        static float HeadingFromReference(const SysMagVector3 &reference,
                                          const SysMagVector3 &current);
        static float NormalizeSignedDeg(float value);
    };
}
