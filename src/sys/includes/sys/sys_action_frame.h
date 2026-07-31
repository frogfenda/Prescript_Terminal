/*
【模块职责】为短时离散动作建立“起手机身坐标”，统一提供陀螺仪相对姿态、动态去重力和
主脉冲/反向回摆相位特征。
【分层边界】本模块只做SYS内部数学处理，不读取LSM6DSL、不认识横斩/敲击等业务语义，
也不发布事件；SysMotion仍是唯一采样所有者，具体识别器负责阈值和类别。
【坐标契约】输入必须是SysMotion已经换算的V4B机身右手坐标，单位为g、dps、us。
*/
#pragma once

#include <Arduino.h>

#include "sys/sys_pose_solver.h"

namespace SysActionFrame
{
    /** ExtractPhase当前固定栈数组与双蛇杖环形窗口共同支持的最大帧数。 */
    constexpr uint8_t MAX_PHASE_SAMPLES = 96;

    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vector3() = default;
        Vector3(float x_value, float y_value, float z_value)
            : x(x_value), y(y_value), z(z_value) {}
    };

    struct Quaternion
    {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Quaternion() = default;
        Quaternion(float w_value, float x_value, float y_value, float z_value)
            : w(w_value), x(x_value), y(y_value), z(z_value) {}
    };

    /** 一帧已经转换到固定起手坐标的派生量；不保留原始传感器读数。 */
    struct FrameSample
    {
        uint32_t timestamp_us = 0;
        Vector3 gyro_local_dps;
        Vector3 linear_accel_local_g;
        float relative_angle_deg = 0.0f;
    };

    /**
     * 通用“主脉冲—反向回摆”事实。角速度面积单位为度，冲量单位为g·s，时间为ms。
     * 本结构没有业务阈值；滚动、敲击和双蛇杖可分别选择自己需要的字段进行分类。
     */
    struct PhaseFeatures
    {
        uint32_t primary_timestamp_us = 0;
        Vector3 main_axis_local;
        float gyro_peak_dps = 0.0f;
        float primary_peak_dps = 0.0f;
        float return_peak_dps = 0.0f;
        float primary_area_deg = 0.0f;
        float return_area_deg = 0.0f;
        float return_to_primary_ratio = 0.0f;
        float primary_offset_ms = 0.0f;
        float main_duration_ms = 0.0f;
        float return_delay_ms = -1.0f;
        float max_main_relative_angle_deg = 0.0f;
        float end_main_relative_angle_deg = 0.0f;
        float linear_accel_peak_g = 0.0f;
        Vector3 linear_accel_peak_local_g;
        Vector3 linear_impulse_local_gs;
        /**
         * 从触发前30ms到触发后300ms积分线性加速度得到的最大速度向量，单位为g·s。
         * 它描述设备真正划过的主路径；与单个冲击峰相比，对横/竖/斜向动作更有物理意义。
         */
        float trajectory_peak_speed_gs = 0.0f;
        Vector3 trajectory_peak_velocity_local_gs;
    };

    class Integrator
    {
    public:
        /**
         * 用确认静止的起手重力和陀螺仪零偏开始一次短时相对积分。
         * 重力模长必须在0.5～1.5g；成功后相对姿态为单位四元数，首帧dt为0。
         */
        bool Reset(const Vector3 &anchor_gravity_body,
                   const Vector3 &gyro_bias_body_dps);

        /**
         * 推进一帧机身坐标样本。动作期间只用陀螺仪更新相对姿态，不让高冲击加速度修正姿态；
         * 时间倒退或间隔超过100ms时返回false并使本实例失效，调用方必须重新静止Reset。
         */
        bool Update(uint32_t timestamp_us,
                    const SysPose::ImuSample &body_imu,
                    FrameSample *out);

        bool IsValid() const { return valid_; }

    private:
        Vector3 anchor_gravity_body_;
        Vector3 gyro_bias_body_dps_;
        Quaternion relative_orientation_;
        uint32_t last_timestamp_us_ = 0;
        bool has_timestamp_ = false;
        bool valid_ = false;
    };

    /**
     * 从按时间升序的局部帧提取主脉冲、回摆、角位移和线性冲量。
     * sample_count不得超过MAX_PHASE_SAMPLES；返回false表示帧数不足、主峰不存在或主轴
     * 角位移接近零。函数不分配堆内存。
     */
    bool ExtractPhase(const FrameSample *samples,
                      uint8_t sample_count,
                      uint32_t trigger_us,
                      PhaseFeatures *out);

    float Norm(const Vector3 &value);
    Vector3 Unit(const Vector3 &value);
    float Dot(const Vector3 &left, const Vector3 &right);
}
