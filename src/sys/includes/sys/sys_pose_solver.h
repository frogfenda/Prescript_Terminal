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
        void Update(const ImuSample &imu, const MagSample *mag = nullptr);
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
        float invSampleFreq_ = 1.0f / 512.0f;
        bool valid_ = false;
        bool magUsed_ = false;

        void UpdateImu(float gx, float gy, float gz, float ax, float ay, float az);
        void ApplyFeedback(float halfex, float halfey, float halfez, float &gx, float &gy, float &gz);
        void Integrate(float gx, float gy, float gz);
        EulerAngles GetEuler(bool invertYawForDisplay) const;
    };
}
