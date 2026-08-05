/*
【模块职责】从固定入口摆放建立短时人体坐标，持续估计航向/倾斜漂移和人体坐标去重力加速度。
【坐标合同】入口时屏幕朝上、底边朝向使用者，因此 HumanX=BodyX（人体右）、
HumanY=BodyY（人体前）、HumanZ=入口重力反方向（人体上）。六轴 IMU 不能验证底边方向，
该方向由用户摆放保证，并定义没有磁力计时无法从传感器恢复的航向零点。
【分层边界】本模块只做数学和统计，不读取 I2C、不绘制 UI、不访问文件，也不改变正式动作识别器。
*/
#pragma once

#include <stdint.h>

#include "sys/sys_pose_solver.h"

namespace SysHumanFrame
{
    enum class Status : uint8_t
    {
        Calibrating = 0,
        Tracking,
        Discontinuous,
    };

    /**
     * 调试器的一帧输入。调用者必须复制 SysMotion 的 body_imu 和同帧元数据；本模块不会访问传感器。
     */
    struct InputSample
    {
        uint32_t sequence = 0;
        uint32_t timestamp_us = 0;
        bool accel_fresh = false;
        bool gyro_fresh = false;
        SysPose::ImuSample body_imu;
    };

    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        // Arduino 工具链仍按 GNU++11 编译；显式构造函数保证三分量初始化不依赖 C++14 聚合规则。
        constexpr Vector3() = default;
        constexpr Vector3(float x_value, float y_value, float z_value)
            : x(x_value), y(y_value), z(z_value) {}
    };

    /**
     * 一份可直接复制给 UI 的只读快照。角度单位为度，加速度单位为 g，时间单位为微秒。
     * first_* 数组中的 0 表示运行期间尚未首次达到该阈值。
     */
    struct Snapshot
    {
        Status status = Status::Calibrating;
        uint64_t elapsed_us = 0;
        uint32_t processed_samples = 0;
        uint32_t missed_sequences = 0;
        uint32_t gaps_over_30ms = 0;
        uint32_t stale_accel_samples = 0;
        uint32_t discontinuities = 0;

        Vector3 gyro_bias_dps;
        // 静止辅助通道当前的“机身坐标→入口人体坐标”旋转，供只读姿态可视化使用。
        SysPose::Quaternion aided_orientation;
        Vector3 linear_accel_human_g;
        Vector3 gyro_only_linear_accel_human_g;
        float linear_residual_g = 0.0f;
        float linear_rms_g = 0.0f;
        float linear_max_g = 0.0f;

        float gyro_yaw_drift_deg = 0.0f;
        float gyro_pitch_drift_deg = 0.0f;
        float gyro_roll_drift_deg = 0.0f;
        float aided_yaw_drift_deg = 0.0f;
        float aided_pitch_drift_deg = 0.0f;
        float aided_roll_drift_deg = 0.0f;
        float yaw_drift_rate_deg_per_min = 0.0f;

        // 纯陀螺仪航向漂移首次达到 1°/3°/5°/10° 的累计时间。
        uint64_t first_yaw_threshold_us[4] = {};
        // 纯陀螺仪合成倾斜漂移首次达到 1°/3°/5° 的累计时间。
        uint64_t first_tilt_threshold_us[3] = {};
    };

    class Tracker
    {
    public:
        /** 清空校准、姿态和统计，从下一帧重新等待固定方向静止入口。 */
        void Begin();

        /**
         * 顺序推进一帧 V4B 机身坐标样本。只有 fresh 陀螺仪帧才推进姿态；超过 100ms 的断点
         * 会进入 Discontinuous 并冻结结果，必须显式 Begin() 重新校准，不能把未知运动伪装成连续姿态。
         * 返回 true 表示本次完成了校准或推进了一帧跟踪数据。
         */
        bool Update(const InputSample &sample);

        Snapshot GetSnapshot() const { return snapshot_; }

    private:
        /* 64帧约覆盖0.6秒实板样本，用于确认零偏窗口稳定；该固定数组只属于追踪器实例，不分配堆。 */
        static constexpr uint16_t CALIBRATION_SAMPLE_CAPACITY = 64;

        SysPose::MahonySolver gyro_solver_;
        SysPose::MahonySolver aided_solver_;
        Snapshot snapshot_;

        bool calibration_started_ = false;
        uint32_t calibration_started_us_ = 0;
        bool quiet_started_ = false;
        uint32_t quiet_since_us_ = 0;
        Vector3 accel_sum_;
        Vector3 gyro_sum_;
        Vector3 accel_samples_[CALIBRATION_SAMPLE_CAPACITY] = {};
        Vector3 gyro_samples_[CALIBRATION_SAMPLE_CAPACITY] = {};
        uint16_t quiet_sample_count_ = 0;
        uint16_t quiet_sample_write_ = 0;

        uint32_t last_sequence_ = 0;
        uint32_t last_timestamp_us_ = 0;
        float gravity_magnitude_g_ = 1.0f;
        float initial_gyro_pitch_deg_ = 0.0f;
        float initial_gyro_roll_deg_ = 0.0f;
        float initial_aided_pitch_deg_ = 0.0f;
        float initial_aided_roll_deg_ = 0.0f;
        float previous_gyro_yaw_deg_ = 0.0f;
        float previous_aided_yaw_deg_ = 0.0f;
        double linear_residual_square_sum_ = 0.0;
        uint32_t linear_residual_count_ = 0;

        void ResetQuietEvidence();
        void AppendCalibrationSample(const InputSample &sample);
        bool UpdateCalibration(const InputSample &sample);
        bool FinishCalibration(const InputSample &sample);
        void UpdateThresholdTimes();
    };
}
