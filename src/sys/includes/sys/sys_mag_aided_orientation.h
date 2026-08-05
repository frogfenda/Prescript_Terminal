/*
【模块职责】在六轴Body→Human姿态之外维护一条独立的“磁辅助姿态”。它只保存一个绕Human Z
轴的缓慢航向修正量，再把该修正左乘到调用者提供的基础四元数；俯仰、横滚和加速度解算不在本模块内。
【输入边界】调用者传入基础四元数、已扣除本轮静态零偏的陀螺模长、加速度模长偏差，以及
SysMagHeading已经完成时间对齐和质量门的只读快照。本模块不访问I2C、SysMotion、SysMag、文件或UI。
【安全策略】动作期间立即冻结；连续安静200ms后才允许以最大4度/秒纠偏。磁场过期、受扰、置信度
不足或尚无入口参考时继续冻结并保留最后修正，绝不把异常磁场直接写进正式姿态或动作识别。
*/
#pragma once

#include <stdint.h>

#include "sys/sys_mag_heading_constraint.h"
#include "sys/sys_pose_solver.h"

namespace SysMagAidedOrientation
{
    enum class State : uint8_t
    {
        WaitingHeading = 0,
        Moving,
        Settling,
        MagneticRejected,
        Correcting,
        Holding,
        InvalidOrientation,
    };

    struct Snapshot
    {
        bool orientation_valid = false;
        bool quiet_ready = false;
        bool correction_active = false;
        SysPose::Quaternion orientation;
        float gyro_magnitude_dps = 0.0f;
        float accel_delta_g = 0.0f;
        float filtered_heading_error_deg = 0.0f;
        float correction_deg = 0.0f;
        float remaining_correction_deg = 0.0f;
        uint32_t quiet_elapsed_ms = 0;
        uint32_t heading_samples_used = 0;
        State state = State::WaitingHeading;
    };

    class Tracker
    {
    public:
        /** 清空低通、静止证据和航向修正；下一帧有效基础姿态从零修正开始。 */
        void Begin();

        /**
         * 顺序推进一帧磁辅助姿态。timestamp_us必须与本帧基础姿态对应且单调递增；gyro_magnitude_dps
         * 必须已经扣除人体追踪器本轮gyro_bias，accel_delta_g为|加速度模长-1g|。
         * 返回true表示输出四元数有效；即使磁场被拒绝也会返回冻结修正后的基础姿态。
         */
        bool Update(uint32_t timestamp_us,
                    const SysPose::Quaternion &base_orientation,
                    float gyro_magnitude_dps,
                    float accel_delta_g,
                    const SysMagHeading::Snapshot &heading);

        Snapshot GetSnapshot() const { return snapshot_; }
        static const char *StateName(State state);

    private:
        static constexpr float QUIET_GYRO_MAX_DPS = 15.0f;
        static constexpr float QUIET_ACCEL_MAX_DELTA_G = 0.12f;
        static constexpr uint32_t QUIET_CONFIRM_US = 200000;
        static constexpr uint32_t MAX_UPDATE_GAP_US = 100000;
        static constexpr float HEADING_FILTER_TAU_S = 0.35f;
        static constexpr float MAX_CORRECTION_RATE_DPS = 4.0f;
        static constexpr float CORRECTION_DEADBAND_DEG = 0.25f;

        Snapshot snapshot_;
        uint32_t last_update_us_ = 0;
        uint32_t quiet_since_us_ = 0;
        uint32_t last_heading_sample_count_ = 0;
        uint32_t last_heading_timestamp_us_ = 0;
        bool filtered_heading_valid_ = false;

        static bool NormalizeQuaternion(SysPose::Quaternion *value);
        static float NormalizeSignedDeg(float value);
        static float WrappedDeltaDeg(float target, float current);
        static SysPose::Quaternion ApplyHumanYaw(const SysPose::Quaternion &base,
                                                 float correction_deg);
        void UpdateHeadingFilter(const SysMagHeading::Snapshot &heading);
    };
}
