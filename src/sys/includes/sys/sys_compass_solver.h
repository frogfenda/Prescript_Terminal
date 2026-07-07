#pragma once
#include <Arduino.h>

namespace SysCompass
{
    struct MagneticSample
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct AttitudeSample
    {
        bool valid = false;
        float rollDeg = 0.0f;
        float pitchDeg = 0.0f;
    };

    struct Result
    {
        bool valid = false;
        float headingDeg = 0.0f;
        float tiltHeadingDeg = 0.0f;
        const char *direction = "N";
    };

    float NormalizeDeg360(float deg);
    const char *DirectionName(float deg);
    float HeadingDeg(const MagneticSample &mag);
    float TiltCompensatedHeadingDeg(const MagneticSample &mag, float rollDeg, float pitchDeg);
    Result Solve(const MagneticSample &mag, const AttitudeSample *attitude = nullptr);
}
