#pragma once
#include <Arduino.h>

namespace SysPose
{
    struct ImuSample
    {
        float axG = 0.0f;
        float ayG = 0.0f;
        float azG = 0.0f;
        float gxDps = 0.0f;
        float gyDps = 0.0f;
        float gzDps = 0.0f;
    };

    struct MagSample
    {
        bool valid = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Quaternion
    {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct EulerAngles
    {
        float rollDeg = 0.0f;
        float pitchDeg = 0.0f;
        float yawDeg = 0.0f;
    };

    struct Result
    {
        bool valid = false;
        bool magUsed = false;
        Quaternion quaternion;
        EulerAngles euler;
    };

    class MahonySolver
    {
    public:
        void Begin(float sampleHz);
        void Reset();
        /**
         * 【接口说明】用一帧确认静止的加速度建立当前重力姿态，并清空积分反馈；偏航归零。
         * 【适用场景】离散动作识别器在静止门控完成后快速重锚，避免从单位四元数缓慢收敛；连续
         * 运动页面仍应使用 Begin/Update 保持平滑，不能在运动中反复调用本接口。
         * 【返回】加速度模长过小或数据无效时返回 false，且保留调用前姿态不变。
         */
        bool ResetFromAccel(const ImuSample &imu);
        /**
         * 【兼容接口】按Begin(sampleHz)配置的固定步长更新一次。仅适用于调用节拍确实稳定的旧消费者；
         * 有真实时间戳的采样链路应调用UpdateWithDeltaSeconds，不能把丢帧仍解释成一个固定周期。
         */
        void Update(const ImuSample &imu, const MagSample *mag = nullptr);
        /**
         * 【接口说明】使用调用者提供的真实采样间隔更新一次姿态，单位为秒。
         * 【返回】deltaSeconds非有限、<=0或>0.1秒时拒绝更新并返回false；长断点应由调用者重置姿态/
         * 动作状态，不能通过钳位伪装成连续运动。成功更新返回true。
         * 【线程约束】与Update/ResetFromAccel相同，只能由该解算器实例的单一所有者顺序调用。
         */
        bool UpdateWithDeltaSeconds(const ImuSample &imu,
                                    float deltaSeconds,
                                    const MagSample *mag = nullptr);
        Result GetResult(bool invertYawForDisplay = true) const;
        Quaternion GetQuaternion() const;

    private:
        float twoKp_ = 1.0f;
        float twoKi_ = 0.0f;
        float q0_ = 1.0f;
        float q1_ = 0.0f;
        float q2_ = 0.0f;
        float q3_ = 0.0f;
        float integralFBx_ = 0.0f;
        float integralFBy_ = 0.0f;
        float integralFBz_ = 0.0f;
        float defaultDeltaSeconds_ = 1.0f / 512.0f;
        float activeDeltaSeconds_ = 1.0f / 512.0f;
        bool valid_ = false;
        bool magUsed_ = false;

        void UpdateImu(float gx, float gy, float gz, float ax, float ay, float az);
        void UpdateCore(const ImuSample &imu, const MagSample *mag);
        void ApplyFeedback(float halfex, float halfey, float halfez, float &gx, float &gy, float &gz);
        void Integrate(float gx, float gy, float gz);
        EulerAngles GetEuler(bool invertYawForDisplay) const;
    };
}
