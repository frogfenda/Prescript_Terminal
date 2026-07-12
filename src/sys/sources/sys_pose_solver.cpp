#include "sys/sys_pose_solver.h"
#include "sys/sys_compass_solver.h"
#include <math.h>

namespace
{
    static constexpr float RAD_TO_DEG_F = 57.2957795f;
    static constexpr float DEG_TO_RAD_F = 0.0174532925f;

    float InvSqrt(float value)
    {
        return 1.0f / sqrtf(value);
    }
}

namespace SysPose
{
    void MahonySolver::Begin(float sampleHz)
    {
        if (sampleHz > 0.0f)
            invSampleFreq_ = 1.0f / sampleHz;
        Reset();
    }

    void MahonySolver::Reset()
    {
        q0_ = 1.0f;
        q1_ = 0.0f;
        q2_ = 0.0f;
        q3_ = 0.0f;
        integralFBx_ = 0.0f;
        integralFBy_ = 0.0f;
        integralFBz_ = 0.0f;
        valid_ = false;
        magUsed_ = false;
    }

    void MahonySolver::Update(const ImuSample &imu, const MagSample *mag)
    {
        bool useMag = mag && mag->valid && !(mag->x == 0.0f && mag->y == 0.0f && mag->z == 0.0f);
        magUsed_ = useMag;

        if (!useMag)
        {
            UpdateImu(imu.gxDps, imu.gyDps, imu.gzDps, imu.axG, imu.ayG, imu.azG);
            valid_ = true;
            return;
        }

        float gx = imu.gxDps * DEG_TO_RAD_F;
        float gy = imu.gyDps * DEG_TO_RAD_F;
        float gz = imu.gzDps * DEG_TO_RAD_F;
        float ax = imu.axG;
        float ay = imu.ayG;
        float az = imu.azG;
        float mx = mag->x;
        float my = mag->y;
        float mz = mag->z;

        if (!(ax == 0.0f && ay == 0.0f && az == 0.0f))
        {
            float recipNorm = InvSqrt(ax * ax + ay * ay + az * az);
            ax *= recipNorm;
            ay *= recipNorm;
            az *= recipNorm;

            recipNorm = InvSqrt(mx * mx + my * my + mz * mz);
            mx *= recipNorm;
            my *= recipNorm;
            mz *= recipNorm;

            float q0q0 = q0_ * q0_;
            float q0q1 = q0_ * q1_;
            float q0q2 = q0_ * q2_;
            float q0q3 = q0_ * q3_;
            float q1q1 = q1_ * q1_;
            float q1q2 = q1_ * q2_;
            float q1q3 = q1_ * q3_;
            float q2q2 = q2_ * q2_;
            float q2q3 = q2_ * q3_;
            float q3q3 = q3_ * q3_;

            float hx = 2.0f * mx * (0.5f - q2q2 - q3q3) +
                       2.0f * my * (q1q2 - q0q3) +
                       2.0f * mz * (q1q3 + q0q2);
            float hy = 2.0f * mx * (q1q2 + q0q3) +
                       2.0f * my * (0.5f - q1q1 - q3q3) +
                       2.0f * mz * (q2q3 - q0q1);
            float bx = sqrtf(hx * hx + hy * hy);
            float bz = 2.0f * mx * (q1q3 - q0q2) +
                       2.0f * my * (q2q3 + q0q1) +
                       2.0f * mz * (0.5f - q1q1 - q2q2);

            float halfvx = q1q3 - q0q2;
            float halfvy = q0q1 + q2q3;
            float halfvz = q0q0 - 0.5f + q3q3;
            float halfwx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
            float halfwy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
            float halfwz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

            float halfex = (ay * halfvz - az * halfvy) + (my * halfwz - mz * halfwy);
            float halfey = (az * halfvx - ax * halfvz) + (mz * halfwx - mx * halfwz);
            float halfez = (ax * halfvy - ay * halfvx) + (mx * halfwy - my * halfwx);

            ApplyFeedback(halfex, halfey, halfez, gx, gy, gz);
        }

        Integrate(gx, gy, gz);
        valid_ = true;
    }

    void MahonySolver::UpdateImu(float gx, float gy, float gz, float ax, float ay, float az)
    {
        gx *= DEG_TO_RAD_F;
        gy *= DEG_TO_RAD_F;
        gz *= DEG_TO_RAD_F;

        if (!(ax == 0.0f && ay == 0.0f && az == 0.0f))
        {
            float recipNorm = InvSqrt(ax * ax + ay * ay + az * az);
            ax *= recipNorm;
            ay *= recipNorm;
            az *= recipNorm;

            float halfvx = q1_ * q3_ - q0_ * q2_;
            float halfvy = q0_ * q1_ + q2_ * q3_;
            float halfvz = q0_ * q0_ - 0.5f + q3_ * q3_;

            float halfex = ay * halfvz - az * halfvy;
            float halfey = az * halfvx - ax * halfvz;
            float halfez = ax * halfvy - ay * halfvx;

            ApplyFeedback(halfex, halfey, halfez, gx, gy, gz);
        }

        Integrate(gx, gy, gz);
    }

    void MahonySolver::ApplyFeedback(float halfex, float halfey, float halfez, float &gx, float &gy, float &gz)
    {
        if (twoKi_ > 0.0f)
        {
            integralFBx_ += twoKi_ * halfex * invSampleFreq_;
            integralFBy_ += twoKi_ * halfey * invSampleFreq_;
            integralFBz_ += twoKi_ * halfez * invSampleFreq_;
            gx += integralFBx_;
            gy += integralFBy_;
            gz += integralFBz_;
        }
        else
        {
            integralFBx_ = 0.0f;
            integralFBy_ = 0.0f;
            integralFBz_ = 0.0f;
        }

        gx += twoKp_ * halfex;
        gy += twoKp_ * halfey;
        gz += twoKp_ * halfez;
    }

    void MahonySolver::Integrate(float gx, float gy, float gz)
    {
        gx *= 0.5f * invSampleFreq_;
        gy *= 0.5f * invSampleFreq_;
        gz *= 0.5f * invSampleFreq_;

        float qa = q0_;
        float qb = q1_;
        float qc = q2_;

        q0_ += (-qb * gx - qc * gy - q3_ * gz);
        q1_ += (qa * gx + qc * gz - q3_ * gy);
        q2_ += (qa * gy - qb * gz + q3_ * gx);
        q3_ += (qa * gz + qb * gy - qc * gx);

        float recipNorm = InvSqrt(q0_ * q0_ + q1_ * q1_ + q2_ * q2_ + q3_ * q3_);
        q0_ *= recipNorm;
        q1_ *= recipNorm;
        q2_ *= recipNorm;
        q3_ *= recipNorm;
    }

    Quaternion MahonySolver::GetQuaternion() const
    {
        Quaternion q;
        q.w = q0_;
        q.x = q1_;
        q.y = q2_;
        q.z = q3_;
        return q;
    }

    EulerAngles MahonySolver::GetEuler(bool invertYawForDisplay) const
    {
        float rollRad = atan2f(q0_ * q1_ + q2_ * q3_, 0.5f - q1_ * q1_ - q2_ * q2_);

        float pitchValue = -2.0f * (q1_ * q3_ - q0_ * q2_);
        if (pitchValue > 1.0f)
            pitchValue = 1.0f;
        if (pitchValue < -1.0f)
            pitchValue = -1.0f;
        float pitchRad = asinf(pitchValue);

        float yawRad = atan2f(q1_ * q2_ + q0_ * q3_, 0.5f - q2_ * q2_ - q3_ * q3_);
        if (invertYawForDisplay)
            yawRad = -yawRad;

        EulerAngles euler;
        euler.rollDeg = rollRad * RAD_TO_DEG_F;
        euler.pitchDeg = pitchRad * RAD_TO_DEG_F;
        euler.yawDeg = SysCompass::NormalizeDeg360(yawRad * RAD_TO_DEG_F);
        return euler;
    }

    Result MahonySolver::GetResult(bool invertYawForDisplay) const
    {
        Result result;
        result.valid = valid_;
        result.magUsed = magUsed_;
        result.quaternion = GetQuaternion();
        result.euler = GetEuler(invertYawForDisplay);
        return result;
    }
}
