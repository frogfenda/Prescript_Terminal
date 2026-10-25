/*
【模块职责】磁力计三维硬铁/软铁校准的纯数学接口。它把传感器坐标中的椭球样本拟合为原点球面，
不读取硬件、不分配内存、不访问文件，可由固件和宿主回归测试链接同一实现。
*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace SysMagCalibration
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        // Arduino工具链使用GNU++11；显式构造保证带默认成员初始化时仍可用三分量临时值赋值。
        constexpr Vector3() = default;
        constexpr Vector3(float x_value, float y_value, float z_value)
            : x(x_value), y(y_value), z(z_value) {}
    };

    enum class Failure : uint8_t
    {
        None = 0,
        TooFewSamples,
        InsufficientSpan,
        InsufficientCoverage,
        SingularFit,
        InvalidEllipsoid,
        ExcessiveDistortion,
        ExcessiveResidual,
    };

    struct Result
    {
        bool valid = false;
        Failure failure = Failure::TooFewSamples;
        uint32_t sample_count = 0;
        uint8_t coverage_bins = 0;
        float coverage_ratio = 0.0f;
        Vector3 span_uT;
        Vector3 bias_uT;
        float soft_iron[3][3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        float reference_field_uT = 0.0f;
        float residual_rms_ratio = 0.0f;
        float residual_max_ratio = 0.0f;
        float condition_ratio = 0.0f;
    };

    /**
     * 对至少200个覆盖完整三维方向的微特斯拉样本执行九参数代数椭球拟合。soft_iron 保持
     * 行列式为1，只修正各向异性；整体场强由 reference_field_uT 单独报告。
     */
    Result Fit(const Vector3 *samples, size_t count);

    /** 应用 Result 中的硬铁偏置和3×3软铁矩阵；result.valid=false 时仍按其当前数值计算。 */
    Vector3 Apply(const Result &result, const Vector3 &sample);

    const char *FailureName(Failure failure);
}
