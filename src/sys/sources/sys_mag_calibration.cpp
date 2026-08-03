/*
【模块职责】实现三维磁场椭球拟合、对称矩阵平方根和质量评价。
【数值策略】先用三轴极值中心和最大半跨度把输入归一化到约[-1,1]，再解9×9正规方程，避免
直接用微特斯拉平方项造成严重条件数差；最终矩阵归一为行列式1，保留传感器的绝对量纲。
*/
#include "sys/sys_mag_calibration.h"

#include <math.h>

namespace
{
    static constexpr size_t PARAMETER_COUNT = 9;
    static constexpr size_t MIN_SAMPLE_COUNT = 200;
    static constexpr float MIN_AXIS_SPAN_UT = 20.0f;
    static constexpr uint8_t COVERAGE_BIN_COUNT = 26;
    static constexpr uint8_t MIN_COVERAGE_BINS = 18;
    static constexpr double PIVOT_EPSILON = 1.0e-10;
    static constexpr double EIGEN_EPSILON = 1.0e-8;
    static constexpr float MAX_CONDITION_RATIO = 8.0f;
    static constexpr float MAX_RMS_RESIDUAL_RATIO = 0.10f;

    using Matrix3 = double[3][3];

    bool IsFiniteVector(const SysMagCalibration::Vector3 &value)
    {
        return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
    }

    double Determinant3(const Matrix3 &m)
    {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    bool Invert3(const Matrix3 &m, Matrix3 &out)
    {
        const double determinant = Determinant3(m);
        if (!isfinite(determinant) || fabs(determinant) < PIVOT_EPSILON)
            return false;
        const double inverse = 1.0 / determinant;
        out[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inverse;
        out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inverse;
        out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inverse;
        out[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inverse;
        out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inverse;
        out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inverse;
        out[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inverse;
        out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inverse;
        out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inverse;
        return true;
    }

    bool SolveLinear9(double augmented[PARAMETER_COUNT][PARAMETER_COUNT + 1],
                      double solution[PARAMETER_COUNT])
    {
        for (size_t column = 0; column < PARAMETER_COUNT; ++column)
        {
            size_t pivot = column;
            double pivot_abs = fabs(augmented[pivot][column]);
            for (size_t row = column + 1; row < PARAMETER_COUNT; ++row)
            {
                const double candidate = fabs(augmented[row][column]);
                if (candidate > pivot_abs)
                {
                    pivot = row;
                    pivot_abs = candidate;
                }
            }
            if (!isfinite(pivot_abs) || pivot_abs < PIVOT_EPSILON)
                return false;
            if (pivot != column)
            {
                for (size_t entry = column; entry <= PARAMETER_COUNT; ++entry)
                {
                    const double temporary = augmented[column][entry];
                    augmented[column][entry] = augmented[pivot][entry];
                    augmented[pivot][entry] = temporary;
                }
            }

            const double divisor = augmented[column][column];
            for (size_t entry = column; entry <= PARAMETER_COUNT; ++entry)
                augmented[column][entry] /= divisor;

            for (size_t row = 0; row < PARAMETER_COUNT; ++row)
            {
                if (row == column)
                    continue;
                const double factor = augmented[row][column];
                if (factor == 0.0)
                    continue;
                for (size_t entry = column; entry <= PARAMETER_COUNT; ++entry)
                    augmented[row][entry] -= factor * augmented[column][entry];
            }
        }

        for (size_t index = 0; index < PARAMETER_COUNT; ++index)
        {
            solution[index] = augmented[index][PARAMETER_COUNT];
            if (!isfinite(solution[index]))
                return false;
        }
        return true;
    }

    /** Jacobi迭代求实对称3×3矩阵的特征值和正交特征向量。 */
    bool EigenSymmetric3(const Matrix3 &input, double eigenvalues[3], Matrix3 &eigenvectors)
    {
        Matrix3 a = {};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                a[row][column] = input[row][column];
                eigenvectors[row][column] = row == column ? 1.0 : 0.0;
            }
        }

        for (int iteration = 0; iteration < 32; ++iteration)
        {
            int p = 0;
            int q = 1;
            double largest = fabs(a[p][q]);
            if (fabs(a[0][2]) > largest) { p = 0; q = 2; largest = fabs(a[0][2]); }
            if (fabs(a[1][2]) > largest) { p = 1; q = 2; largest = fabs(a[1][2]); }
            if (largest < 1.0e-12)
                break;

            const double angle = 0.5 * atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
            const double cosine = cos(angle);
            const double sine = sin(angle);

            for (int index = 0; index < 3; ++index)
            {
                if (index == p || index == q)
                    continue;
                const double aip = a[index][p];
                const double aiq = a[index][q];
                a[index][p] = a[p][index] = cosine * aip - sine * aiq;
                a[index][q] = a[q][index] = sine * aip + cosine * aiq;
            }
            const double app = a[p][p];
            const double aqq = a[q][q];
            const double apq = a[p][q];
            a[p][p] = cosine * cosine * app - 2.0 * sine * cosine * apq + sine * sine * aqq;
            a[q][q] = sine * sine * app + 2.0 * sine * cosine * apq + cosine * cosine * aqq;
            a[p][q] = a[q][p] = 0.0;

            for (int row = 0; row < 3; ++row)
            {
                const double vip = eigenvectors[row][p];
                const double viq = eigenvectors[row][q];
                eigenvectors[row][p] = cosine * vip - sine * viq;
                eigenvectors[row][q] = sine * vip + cosine * viq;
            }
        }

        for (int index = 0; index < 3; ++index)
        {
            eigenvalues[index] = a[index][index];
            if (!isfinite(eigenvalues[index]))
                return false;
        }
        return true;
    }

    uint8_t CoverageBin(const SysMagCalibration::Vector3 &value)
    {
        const float magnitude = sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
        if (!isfinite(magnitude) || magnitude < 0.001f)
            return 0xFF;
        const float inverse = 1.0f / magnitude;
        const float normalized[3] = {value.x * inverse, value.y * inverse, value.z * inverse};
        int code[3] = {};
        for (int axis = 0; axis < 3; ++axis)
            code[axis] = normalized[axis] > 0.35f ? 1 : (normalized[axis] < -0.35f ? -1 : 0);
        const int encoded = (code[0] + 1) * 9 + (code[1] + 1) * 3 + (code[2] + 1);
        if (encoded == 13)
            return 0xFF;
        return static_cast<uint8_t>(encoded > 13 ? encoded - 1 : encoded);
    }
}

namespace SysMagCalibration
{
    Vector3 Apply(const Result &result, const Vector3 &sample)
    {
        const float x = sample.x - result.bias_uT.x;
        const float y = sample.y - result.bias_uT.y;
        const float z = sample.z - result.bias_uT.z;
        Vector3 corrected;
        corrected.x = result.soft_iron[0][0] * x + result.soft_iron[0][1] * y + result.soft_iron[0][2] * z;
        corrected.y = result.soft_iron[1][0] * x + result.soft_iron[1][1] * y + result.soft_iron[1][2] * z;
        corrected.z = result.soft_iron[2][0] * x + result.soft_iron[2][1] * y + result.soft_iron[2][2] * z;
        return corrected;
    }

    Result Fit(const Vector3 *samples, size_t count)
    {
        Result result;
        result.sample_count = static_cast<uint32_t>(count);
        if (!samples || count < MIN_SAMPLE_COUNT)
            return result;

        Vector3 minimum = samples[0];
        Vector3 maximum = samples[0];
        size_t finite_count = 0;
        for (size_t index = 0; index < count; ++index)
        {
            const Vector3 &sample = samples[index];
            if (!IsFiniteVector(sample))
                continue;
            ++finite_count;
            minimum.x = fminf(minimum.x, sample.x); minimum.y = fminf(minimum.y, sample.y); minimum.z = fminf(minimum.z, sample.z);
            maximum.x = fmaxf(maximum.x, sample.x); maximum.y = fmaxf(maximum.y, sample.y); maximum.z = fmaxf(maximum.z, sample.z);
        }
        if (finite_count < MIN_SAMPLE_COUNT)
            return result;

        result.span_uT = {maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z};
        if (result.span_uT.x < MIN_AXIS_SPAN_UT || result.span_uT.y < MIN_AXIS_SPAN_UT ||
            result.span_uT.z < MIN_AXIS_SPAN_UT)
        {
            result.failure = Failure::InsufficientSpan;
            return result;
        }

        const Vector3 provisional_center = {(minimum.x + maximum.x) * 0.5f,
                                            (minimum.y + maximum.y) * 0.5f,
                                            (minimum.z + maximum.z) * 0.5f};
        const double normalization_scale = fmaxf(result.span_uT.x,
                                                  fmaxf(result.span_uT.y, result.span_uT.z)) * 0.5;
        if (!isfinite(normalization_scale) || normalization_scale < 1.0)
        {
            result.failure = Failure::InsufficientSpan;
            return result;
        }

        double normal[PARAMETER_COUNT][PARAMETER_COUNT] = {};
        double target[PARAMETER_COUNT] = {};
        for (size_t index = 0; index < count; ++index)
        {
            const Vector3 &sample = samples[index];
            if (!IsFiniteVector(sample))
                continue;
            const double x = (sample.x - provisional_center.x) / normalization_scale;
            const double y = (sample.y - provisional_center.y) / normalization_scale;
            const double z = (sample.z - provisional_center.z) / normalization_scale;
            const double feature[PARAMETER_COUNT] = {
                x * x, y * y, z * z, 2.0 * x * y, 2.0 * x * z, 2.0 * y * z, x, y, z,
            };
            for (size_t row = 0; row < PARAMETER_COUNT; ++row)
            {
                target[row] += feature[row];
                for (size_t column = 0; column < PARAMETER_COUNT; ++column)
                    normal[row][column] += feature[row] * feature[column];
            }
        }

        double augmented[PARAMETER_COUNT][PARAMETER_COUNT + 1] = {};
        for (size_t row = 0; row < PARAMETER_COUNT; ++row)
        {
            for (size_t column = 0; column < PARAMETER_COUNT; ++column)
                augmented[row][column] = normal[row][column];
            augmented[row][PARAMETER_COUNT] = target[row];
        }
        double parameters[PARAMETER_COUNT] = {};
        if (!SolveLinear9(augmented, parameters))
        {
            result.failure = Failure::SingularFit;
            return result;
        }

        Matrix3 a = {
            {parameters[0], parameters[3], parameters[4]},
            {parameters[3], parameters[1], parameters[5]},
            {parameters[4], parameters[5], parameters[2]},
        };
        Matrix3 inverse_a = {};
        if (!Invert3(a, inverse_a))
        {
            result.failure = Failure::InvalidEllipsoid;
            return result;
        }
        const double b[3] = {parameters[6], parameters[7], parameters[8]};
        double center[3] = {};
        for (int row = 0; row < 3; ++row)
            center[row] = -0.5 * (inverse_a[row][0] * b[0] + inverse_a[row][1] * b[1] + inverse_a[row][2] * b[2]);
        const double center_quadratic = center[0] * (a[0][0] * center[0] + a[0][1] * center[1] + a[0][2] * center[2]) +
                                        center[1] * (a[1][0] * center[0] + a[1][1] * center[1] + a[1][2] * center[2]) +
                                        center[2] * (a[2][0] * center[0] + a[2][1] * center[1] + a[2][2] * center[2]);
        const double k = 1.0 + center_quadratic;
        if (!isfinite(k) || k <= EIGEN_EPSILON)
        {
            result.failure = Failure::InvalidEllipsoid;
            return result;
        }

        Matrix3 shape = {};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                shape[row][column] = a[row][column] / k;

        double eigenvalues[3] = {};
        Matrix3 eigenvectors = {};
        if (!EigenSymmetric3(shape, eigenvalues, eigenvectors))
        {
            result.failure = Failure::InvalidEllipsoid;
            return result;
        }
        double minimum_eigenvalue = eigenvalues[0];
        double maximum_eigenvalue = eigenvalues[0];
        for (int index = 0; index < 3; ++index)
        {
            if (eigenvalues[index] <= EIGEN_EPSILON)
            {
                result.failure = Failure::InvalidEllipsoid;
                return result;
            }
            minimum_eigenvalue = fmin(minimum_eigenvalue, eigenvalues[index]);
            maximum_eigenvalue = fmax(maximum_eigenvalue, eigenvalues[index]);
        }
        result.condition_ratio = static_cast<float>(sqrt(maximum_eigenvalue / minimum_eigenvalue));
        if (!isfinite(result.condition_ratio) || result.condition_ratio > MAX_CONDITION_RATIO)
        {
            result.failure = Failure::ExcessiveDistortion;
            return result;
        }

        const double determinant_shape = eigenvalues[0] * eigenvalues[1] * eigenvalues[2];
        const double determinant_normalizer = pow(determinant_shape, -1.0 / 6.0);
        Matrix3 correction = {};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                for (int eigen = 0; eigen < 3; ++eigen)
                {
                    correction[row][column] += eigenvectors[row][eigen] *
                        sqrt(eigenvalues[eigen]) * determinant_normalizer * eigenvectors[column][eigen];
                }
                result.soft_iron[row][column] = static_cast<float>(correction[row][column]);
            }
        }

        result.bias_uT = {
            static_cast<float>(provisional_center.x + center[0] * normalization_scale),
            static_cast<float>(provisional_center.y + center[1] * normalization_scale),
            static_cast<float>(provisional_center.z + center[2] * normalization_scale),
        };

        double magnitude_sum = 0.0;
        size_t magnitude_count = 0;
        for (size_t index = 0; index < count; ++index)
        {
            if (!IsFiniteVector(samples[index]))
                continue;
            const Vector3 corrected = Apply(result, samples[index]);
            const double magnitude = sqrt(static_cast<double>(corrected.x) * corrected.x +
                                          static_cast<double>(corrected.y) * corrected.y +
                                          static_cast<double>(corrected.z) * corrected.z);
            if (isfinite(magnitude) && magnitude > 0.0)
            {
                magnitude_sum += magnitude;
                ++magnitude_count;
            }
        }
        if (magnitude_count == 0)
        {
            result.failure = Failure::InvalidEllipsoid;
            return result;
        }
        result.reference_field_uT = static_cast<float>(magnitude_sum / magnitude_count);

        bool coverage[COVERAGE_BIN_COUNT] = {};
        double residual_square_sum = 0.0;
        double residual_max = 0.0;
        for (size_t index = 0; index < count; ++index)
        {
            if (!IsFiniteVector(samples[index]))
                continue;
            const Vector3 corrected = Apply(result, samples[index]);
            const uint8_t bin = CoverageBin(corrected);
            if (bin < COVERAGE_BIN_COUNT)
                coverage[bin] = true;
            const double magnitude = sqrt(static_cast<double>(corrected.x) * corrected.x +
                                          static_cast<double>(corrected.y) * corrected.y +
                                          static_cast<double>(corrected.z) * corrected.z);
            const double residual = fabs(magnitude - result.reference_field_uT) / result.reference_field_uT;
            residual_square_sum += residual * residual;
            residual_max = fmax(residual_max, residual);
        }
        for (uint8_t index = 0; index < COVERAGE_BIN_COUNT; ++index)
            result.coverage_bins += coverage[index] ? 1 : 0;
        result.coverage_ratio = static_cast<float>(result.coverage_bins) / COVERAGE_BIN_COUNT;
        result.residual_rms_ratio = static_cast<float>(sqrt(residual_square_sum / finite_count));
        result.residual_max_ratio = static_cast<float>(residual_max);

        if (result.coverage_bins < MIN_COVERAGE_BINS)
        {
            result.failure = Failure::InsufficientCoverage;
            return result;
        }
        if (!isfinite(result.residual_rms_ratio) || result.residual_rms_ratio > MAX_RMS_RESIDUAL_RATIO)
        {
            result.failure = Failure::ExcessiveResidual;
            return result;
        }

        result.valid = true;
        result.failure = Failure::None;
        return result;
    }

    const char *FailureName(Failure failure)
    {
        switch (failure)
        {
        case Failure::None: return "通过";
        case Failure::TooFewSamples: return "样本不足";
        case Failure::InsufficientSpan: return "三轴跨度不足";
        case Failure::InsufficientCoverage: return "方向覆盖不足";
        case Failure::SingularFit: return "拟合矩阵奇异";
        case Failure::InvalidEllipsoid: return "椭球无效";
        case Failure::ExcessiveDistortion: return "软铁畸变过大";
        case Failure::ExcessiveResidual: return "拟合残差过大";
        default: return "未知错误";
        }
    }
}
