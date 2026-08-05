/*
【实现说明】本文件只负责编排现有三个数学模块，不新增第二套传感器采样或姿态积分器。每笔新IMU
先推进六轴Body→Human追踪并保存带时间戳的姿态历史，再尝试时间对齐最新地磁，最后只绕Human Z
限速修正航向。磁质量门失败时保留最后修正，基础六轴姿态与线性加速度仍继续更新。
*/
#include "sys/sys_human_motion.h"

#include <Arduino.h>
#include <math.h>

#include "sys/sys_mag.h"
#include "sys/sys_motion.h"

namespace SysHumanMotion
{
    namespace
    {
        constexpr float DEG_TO_RAD_F = 0.0174532925f;

        SysHumanFrame::Tracker s_base_tracker;
        SysMagHeading::Constraint s_heading_constraint;
        SysMagAidedOrientation::Tracker s_magnetic_tracker;
        Snapshot s_snapshot;
        bool s_started = false;
        uint32_t s_last_motion_sequence = 0;

        float VectorMagnitude(float x, float y, float z)
        {
            return sqrtf(fmaxf(0.0f, x * x + y * y + z * z));
        }

        /**
         * 基础追踪器已经完成Body→Human旋转和去重力。磁辅助姿态是在其左侧再乘Rz(correction)，
         * 因而线性加速度只需执行完全相同的Human Z二维旋转；Z分量和重力扣除结果保持不变。
         */
        SysHumanFrame::Vector3 ApplyMagneticYaw(
            const SysHumanFrame::Vector3 &base_linear_accel,
            float correction_deg)
        {
            const float wrapped_deg = fmodf(correction_deg, 360.0f);
            const float angle = wrapped_deg * DEG_TO_RAD_F;
            const float cosine = cosf(angle);
            const float sine = sinf(angle);
            return {
                cosine * base_linear_accel.x - sine * base_linear_accel.y,
                sine * base_linear_accel.x + cosine * base_linear_accel.y,
                base_linear_accel.z,
            };
        }

        void ResetOwnedState(bool alignment_active)
        {
            s_base_tracker.Begin();
            s_heading_constraint.Begin();
            s_magnetic_tracker.Begin();
            s_snapshot = {};
            s_snapshot.alignment_active = alignment_active;
            s_snapshot.base = s_base_tracker.GetSnapshot();
            s_snapshot.magnetic_heading = s_heading_constraint.GetSnapshot();
            s_snapshot.magnetic_orientation = s_magnetic_tracker.GetSnapshot();
            s_last_motion_sequence = 0;
        }
    }

    void Init()
    {
        ResetOwnedState(false);
        s_started = true;
    }

    void BeginAlignment()
    {
        if (!s_started)
            Init();
        ResetOwnedState(true);
        Serial.println("[人体姿态] 开始入口对齐：请保持屏幕朝上、底边朝向使用者并静止约1秒。");
    }

    bool Update()
    {
        if (!s_started || !s_snapshot.alignment_active)
            return false;

        SysMotionSample motion = {};
        if (!SysMotion_GetLatest(&motion) || motion.sequence == s_last_motion_sequence)
            return false;
        s_last_motion_sequence = motion.sequence;
        s_snapshot.motion_sequence = motion.sequence;
        s_snapshot.motion_timestamp_us = motion.timestamp_us;
        s_snapshot.absolute_linear_accel_fresh = false;

        SysHumanFrame::InputSample input = {};
        input.sequence = motion.sequence;
        input.timestamp_us = motion.timestamp_us;
        input.accel_fresh = motion.accel_fresh;
        input.gyro_fresh = motion.gyro_fresh;
        input.body_imu = motion.body_imu;
        const bool base_updated = s_base_tracker.Update(input);
        s_snapshot.base = s_base_tracker.GetSnapshot();

        if (s_snapshot.base.status != SysHumanFrame::Status::Tracking)
        {
            s_snapshot.absolute_linear_accel_valid = false;
            return true;
        }

        /* Tracker只有在fresh陀螺帧上才推进姿态。若本序号只刷新了其他通道，不能拿上一帧四元数
         * 冒充当前时间姿态，也不能让磁纠偏按不存在的姿态时间步前进；保留上一笔正式输出即可。 */
        if (!base_updated)
        {
            return true;
        }
        s_heading_constraint.PushOrientation(motion.timestamp_us,
                                             s_snapshot.base.aided_orientation);

        SysMagSample magnetic_sample = {};
        if (SysMag_GetLatest(&magnetic_sample))
        {
            /* micros()代表当前主循环年龄基准；地磁通常在同轮IMU之后采样，约束模块会等待下一帧
             * IMU补齐插值右边界，不会把正常的未来数百微秒误判为过期。 */
            s_heading_constraint.Update(micros(),
                                        s_snapshot.base.aided_orientation,
                                        magnetic_sample);
        }
        s_snapshot.magnetic_heading = s_heading_constraint.GetSnapshot();

        const float corrected_gx = motion.body_imu.gxDps - s_snapshot.base.gyro_bias_dps.x;
        const float corrected_gy = motion.body_imu.gyDps - s_snapshot.base.gyro_bias_dps.y;
        const float corrected_gz = motion.body_imu.gzDps - s_snapshot.base.gyro_bias_dps.z;
        const float gyro_magnitude_dps = VectorMagnitude(corrected_gx,
                                                         corrected_gy,
                                                         corrected_gz);
        const float accel_magnitude_g = VectorMagnitude(motion.body_imu.axG,
                                                        motion.body_imu.ayG,
                                                        motion.body_imu.azG);
        const float accel_delta_g = fabsf(accel_magnitude_g - 1.0f);
        s_magnetic_tracker.Update(motion.timestamp_us,
                                  s_snapshot.base.aided_orientation,
                                  gyro_magnitude_dps,
                                  accel_delta_g,
                                  s_snapshot.magnetic_heading);
        s_snapshot.magnetic_orientation = s_magnetic_tracker.GetSnapshot();

        if (s_snapshot.magnetic_orientation.orientation_valid)
        {
            s_snapshot.absolute_linear_accel_human_g = ApplyMagneticYaw(
                s_snapshot.base.linear_accel_human_g,
                s_snapshot.magnetic_orientation.correction_deg);
            s_snapshot.absolute_linear_accel_valid = true;
            s_snapshot.absolute_linear_accel_fresh = motion.accel_fresh;
        }
        else
        {
            s_snapshot.absolute_linear_accel_valid = false;
        }
        return true;
    }

    bool GetSnapshot(Snapshot *out)
    {
        if (!out || !s_started)
            return false;
        *out = s_snapshot;
        return true;
    }
}
