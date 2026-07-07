#include "sys/sys_compass_solver.h"
#include <math.h>

namespace
{
    static constexpr float RAD_TO_DEG_F = 57.2957795f;
    static constexpr float DEG_TO_RAD_F = 0.0174532925f;
}

namespace SysCompass
{
    float NormalizeDeg360(float deg)
    {
        while (deg < 0.0f)
            deg += 360.0f;
        while (deg >= 360.0f)
            deg -= 360.0f;
        return deg;
    }

    const char *DirectionName(float deg)
    {
        static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        int index = (int)((NormalizeDeg360(deg) + 22.5f) / 45.0f) & 0x07;
        return dirs[index];
    }

    float HeadingDeg(const MagneticSample &mag)
    {
        return NormalizeDeg360(-atan2f(mag.y, mag.x) * RAD_TO_DEG_F);
    }

    float TiltCompensatedHeadingDeg(const MagneticSample &mag, float rollDeg, float pitchDeg)
    {
        float roll = rollDeg * DEG_TO_RAD_F;
        float pitch = pitchDeg * DEG_TO_RAD_F;

        float xh = mag.x * cosf(pitch) + mag.z * sinf(pitch);
        float yh = mag.x * sinf(roll) * sinf(pitch) +
                   mag.y * cosf(roll) -
                   mag.z * sinf(roll) * cosf(pitch);

        return NormalizeDeg360(-atan2f(yh, xh) * RAD_TO_DEG_F);
    }

    Result Solve(const MagneticSample &mag, const AttitudeSample *attitude)
    {
        Result result;
        result.valid = !(mag.x == 0.0f && mag.y == 0.0f && mag.z == 0.0f);
        result.headingDeg = HeadingDeg(mag);

        if (attitude && attitude->valid)
            result.tiltHeadingDeg = TiltCompensatedHeadingDeg(mag, attitude->rollDeg, attitude->pitchDeg);
        else
            result.tiltHeadingDeg = result.headingDeg;

        result.direction = DirectionName(result.tiltHeadingDeg);
        return result;
    }
}
