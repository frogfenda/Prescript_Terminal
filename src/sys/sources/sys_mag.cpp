/*
【模块职责】实现独立磁场采样、校准文件、质量门控和诊断日志。
【恢复策略】正常轮询不重试I2C；失败后每5秒在现有Wire1上Reset或重新探测，绝不周期性重建总线。
【实板门槛】QMC封装相对机身的轴映射尚无PCB坐标证据，因此先保留恒等映射并显式标记未验证；
调试数据确认后只修改本文件的SensorToBody和BOARD_AXIS_MAPPING_VERIFIED。
*/
#include "sys/sys_mag.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "sys/sys_constants.h"
#include "sys/sys_fs.h"

namespace
{
    static constexpr uint32_t POLL_INTERVAL_US = 20000; // QMC5883P Normal 50Hz。
    static constexpr uint32_t RECOVERY_INTERVAL_MS = 5000;
    static constexpr uint32_t DIAGNOSTIC_INTERVAL_MS = 500;
    static constexpr uint32_t CALIBRATION_CAPACITY = 1200;
    static constexpr uint16_t CALIBRATION_SCHEMA_VERSION = 1;
    static constexpr float FIELD_WARNING_RATIO = 0.15f;
    static constexpr float FIELD_REJECT_RATIO = 0.25f;
    static constexpr float FIELD_STEP_REJECT_UT = 10.0f;

    // TODO(实板轴向)：完成三轴双方向旋转采集后改为true，并冻结实际带符号轴交换。
    static constexpr bool BOARD_AXIS_MAPPING_VERIFIED = false;

    bool s_started = false;
    bool s_available = false;
    bool s_sleeping = false;
    bool s_has_sample = false;
    bool s_diagnostic_logging = false;
    uint32_t s_next_poll_us = 0;
    uint32_t s_next_recovery_ms = 0;
    uint32_t s_last_diagnostic_ms = 0;
    float s_previous_field_strength_uT = 0.0f;
    SysMagSample s_latest;

    SysMagCalibration::Result s_calibration;
    uint16_t s_calibration_version = 0;
    SysMagCalibration::Vector3 *s_calibration_samples = nullptr;
    uint32_t s_calibration_count = 0;
    bool s_calibration_active = false;

    BSP::Qmc5883::Config RuntimeConfig()
    {
        BSP::Qmc5883::Config config;
        config.range = BSP::Qmc5883::Range::G8;
        config.outputRate = BSP::Qmc5883::OutputDataRate::Hz50;
        config.oversampling1 = BSP::Qmc5883::Oversampling::X8;
        config.oversampling2 = BSP::Qmc5883::Oversampling::X8;
        return config;
    }

    SysMagVector3 SensorToBody(const SysMagVector3 &sensor)
    {
        /*
         * 这是刻意保守的临时恒等映射，不代表实板结论。AxisMappingVerified=false 会阻止它进入融合，
         * 但调试App仍能同时显示sensor/body，便于用户做三轴旋转并冻结真实映射。
         */
        return sensor;
    }

    float Magnitude(const SysMagVector3 &value)
    {
        return sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
    }

    void ScheduleNextPoll()
    {
        s_next_poll_us = micros() + POLL_INTERVAL_US;
    }

    void FreeCalibrationBuffer()
    {
        if (s_calibration_samples)
            heap_caps_free(s_calibration_samples);
        s_calibration_samples = nullptr;
        s_calibration_count = 0;
        s_calibration_active = false;
    }

    bool ReadFloatArray(JsonVariantConst value, float *out, size_t count)
    {
        if (!out || !value.is<JsonArrayConst>())
            return false;
        JsonArrayConst array = value.as<JsonArrayConst>();
        if (array.size() != count)
            return false;
        for (size_t index = 0; index < count; ++index)
        {
            out[index] = array[index].as<float>();
            if (!isfinite(out[index]))
                return false;
        }
        return true;
    }

    bool LoadCalibration()
    {
        s_calibration = {};
        s_calibration_version = 0;
        const String json = SysFS_Read_File(PrescriptConst::MAG_CALIBRATION_FILE);
        if (json.length() == 0)
            return false;

        JsonDocument document;
        const DeserializationError error = deserializeJson(document, json);
        if (error)
        {
            Serial.printf("[地磁-警告] 校准文件解析失败：%s。\n", error.c_str());
            return false;
        }
        const uint16_t schema = document["schema"] | 0;
        if (schema != CALIBRATION_SCHEMA_VERSION || !(document["valid"] | false))
        {
            Serial.printf("[地磁-警告] 校准文件版本或有效标记不匹配：版本=%u。\n", schema);
            return false;
        }

        float bias[3] = {};
        float matrix[9] = {};
        if (!ReadFloatArray(document["bias_uT"], bias, 3) ||
            !ReadFloatArray(document["soft_iron"], matrix, 9))
        {
            Serial.println("[地磁-警告] 校准文件数组格式无效。");
            return false;
        }

        s_calibration.valid = true;
        s_calibration.failure = SysMagCalibration::Failure::None;
        s_calibration.bias_uT = {bias[0], bias[1], bias[2]};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                s_calibration.soft_iron[row][column] = matrix[row * 3 + column];
        s_calibration.reference_field_uT = document["field_uT"] | 0.0f;
        s_calibration.residual_rms_ratio = document["residual_rms"] | 0.0f;
        s_calibration.residual_max_ratio = document["residual_max"] | 0.0f;
        s_calibration.condition_ratio = document["condition"] | 0.0f;
        s_calibration.coverage_bins = document["coverage_bins"] | 0;
        s_calibration.coverage_ratio = document["coverage_ratio"] | 0.0f;
        s_calibration.sample_count = document["samples"] | 0;
        s_calibration_version = document["calibration_version"] | 1;

        if (!isfinite(s_calibration.reference_field_uT) || s_calibration.reference_field_uT < 10.0f ||
            s_calibration.reference_field_uT > 200.0f)
        {
            Serial.printf("[地磁-警告] 校准参考场强无效：%.2fuT。\n", s_calibration.reference_field_uT);
            s_calibration = {};
            s_calibration_version = 0;
            return false;
        }
        Serial.printf("[地磁] 已载入设备校准：版本=%u，场强=%.2fuT，RMS=%.2f%%。\n",
                      s_calibration_version,
                      s_calibration.reference_field_uT,
                      s_calibration.residual_rms_ratio * 100.0f);
        return true;
    }

    bool SaveCalibration(const SysMagCalibration::Result &calibration)
    {
        JsonDocument document;
        document["schema"] = CALIBRATION_SCHEMA_VERSION;
        document["valid"] = calibration.valid;
        document["sensor"] = BSP::Qmc5883::TypeName();
        document["address"] = BSP::Qmc5883::Address();
        document["calibration_version"] = s_calibration_version;
        JsonArray bias = document["bias_uT"].to<JsonArray>();
        bias.add(calibration.bias_uT.x); bias.add(calibration.bias_uT.y); bias.add(calibration.bias_uT.z);
        JsonArray matrix = document["soft_iron"].to<JsonArray>();
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                matrix.add(calibration.soft_iron[row][column]);
        document["field_uT"] = calibration.reference_field_uT;
        document["residual_rms"] = calibration.residual_rms_ratio;
        document["residual_max"] = calibration.residual_max_ratio;
        document["condition"] = calibration.condition_ratio;
        document["coverage_bins"] = calibration.coverage_bins;
        document["coverage_ratio"] = calibration.coverage_ratio;
        document["samples"] = calibration.sample_count;

        String json;
        serializeJson(document, json);
        if (!SysFS_Write_File(PrescriptConst::MAG_CALIBRATION_FILE, json.c_str()))
        {
            Serial.println("[地磁-错误] 无法写入设备校准文件。");
            return false;
        }
        return true;
    }

    void AppendCalibrationSample(const SysMagSample &sample)
    {
        if (!s_calibration_active || !s_calibration_samples || sample.overflow || !sample.fresh ||
            s_calibration_count >= CALIBRATION_CAPACITY)
        {
            return;
        }
        s_calibration_samples[s_calibration_count++] = {
            sample.sensor_uT.x, sample.sensor_uT.y, sample.sensor_uT.z,
        };
    }

    void EvaluateQuality(SysMagSample &sample)
    {
        sample.calibrated = s_calibration.valid;
        sample.axis_mapping_verified = BOARD_AXIS_MAPPING_VERIFIED;
        sample.range_configuration_verified = BSP::Qmc5883::RangeConfigurationVerified();
        sample.calibration_version = s_calibration_version;
        sample.disturbance_reasons = SYS_MAG_DISTURBANCE_NONE;
        sample.confidence = 1.0f;

        if (sample.overflow)
        {
            sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_OVERFLOW;
            sample.confidence = 0.0f;
        }
        if (!s_calibration.valid)
        {
            sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_NOT_CALIBRATED;
            sample.confidence = fminf(sample.confidence, 0.35f);
        }
        if (!BOARD_AXIS_MAPPING_VERIFIED)
        {
            sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_AXIS_UNVERIFIED;
            sample.confidence = fminf(sample.confidence, 0.35f);
        }
        if (!sample.range_configuration_verified)
        {
            sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_RANGE_UNVERIFIED;
            sample.confidence = fminf(sample.confidence, 0.35f);
        }

        if (s_calibration.valid && s_calibration.reference_field_uT > 1.0f)
        {
            const float field_ratio = fabsf(sample.field_strength_uT - s_calibration.reference_field_uT) /
                                      s_calibration.reference_field_uT;
            if (field_ratio >= FIELD_REJECT_RATIO)
            {
                sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_FIELD_STRENGTH;
                sample.confidence = 0.0f;
            }
            else if (field_ratio >= FIELD_WARNING_RATIO)
            {
                sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_FIELD_STRENGTH;
                const float blend = (FIELD_REJECT_RATIO - field_ratio) /
                                    (FIELD_REJECT_RATIO - FIELD_WARNING_RATIO);
                sample.confidence = fminf(sample.confidence, fmaxf(0.0f, blend));
            }
        }

        if (s_previous_field_strength_uT > 1.0f &&
            fabsf(sample.field_strength_uT - s_previous_field_strength_uT) > FIELD_STEP_REJECT_UT)
        {
            sample.disturbance_reasons |= SYS_MAG_DISTURBANCE_FIELD_STEP;
            sample.confidence = 0.0f;
        }
        s_previous_field_strength_uT = sample.field_strength_uT;
        sample.disturbed = (sample.disturbance_reasons &
            (SYS_MAG_DISTURBANCE_OVERFLOW | SYS_MAG_DISTURBANCE_FIELD_STRENGTH |
             SYS_MAG_DISTURBANCE_FIELD_STEP)) != 0;
        sample.fusion_usable = sample.calibrated && sample.axis_mapping_verified &&
                               sample.range_configuration_verified &&
                               !sample.disturbed && sample.confidence >= 0.5f;
    }

    void PrintDiagnostic(const SysMagSample &sample)
    {
        if (!s_diagnostic_logging)
            return;
        const uint32_t now = millis();
        if (now - s_last_diagnostic_ms < DIAGNOSTIC_INTERVAL_MS)
            return;
        s_last_diagnostic_ms = now;
        Serial.printf("[地磁-数据] 序号=%lu 原始=[%d,%d,%d] 传感器=[%.2f,%.2f,%.2f]uT "
                      "校准=[%.2f,%.2f,%.2f]uT 场强=%.2fuT 置信=%.2f 原因=0x%02lX。\n",
                      static_cast<unsigned long>(sample.sequence),
                      sample.raw_x, sample.raw_y, sample.raw_z,
                      sample.sensor_uT.x, sample.sensor_uT.y, sample.sensor_uT.z,
                      sample.calibrated_sensor_uT.x,
                      sample.calibrated_sensor_uT.y,
                      sample.calibrated_sensor_uT.z,
                      sample.field_strength_uT,
                      sample.confidence,
                      static_cast<unsigned long>(sample.disturbance_reasons));
    }

    bool RecoverSensor()
    {
        bool ok = false;
        if (BSP::Qmc5883::Address() != 0)
            ok = BSP::Qmc5883::Reset();
        if (!ok && BSP::Qmc5883::IsPresent(BSP::Qmc5883::ADDRESS_QMC5883P))
            ok = BSP::Qmc5883::Begin(Wire1, BSP::Qmc5883::ADDRESS_QMC5883P, RuntimeConfig());
        if (!ok)
            return false;

        s_available = true;
        s_previous_field_strength_uT = 0.0f;
        ScheduleNextPoll();
        Serial.println("[地磁] QMC5883P 通信已恢复。");
        return true;
    }

    void MarkOffline()
    {
        if (s_available)
        {
            Serial.printf("[地磁-警告] QMC5883采样失败，稍后恢复：错误码=%u。\n",
                          static_cast<unsigned>(BSP::Qmc5883::LastError()));
        }
        s_available = false;
        s_next_recovery_ms = millis() + RECOVERY_INTERVAL_MS;
    }
}

bool SysMag_Init()
{
    s_started = true;
    s_available = false;
    s_sleeping = false;
    s_has_sample = false;
    s_latest = {};
    s_next_poll_us = 0;
    s_next_recovery_ms = 0;
    s_previous_field_strength_uT = 0.0f;
    FreeCalibrationBuffer();
    LoadCalibration();

    if (!BSP::Qmc5883::Begin(Wire1, BSP::Qmc5883::ADDRESS_QMC5883P, RuntimeConfig()))
    {
        Serial.printf("[地磁-警告] QMC5883P 初始化失败，地磁功能暂不可用：错误码=%u。\n",
                      static_cast<unsigned>(BSP::Qmc5883::LastError()));
        s_next_recovery_ms = millis() + RECOVERY_INTERVAL_MS;
        return false;
    }

    s_available = true;
    ScheduleNextPoll();
    Serial.printf("[地磁] %s已初始化：地址=0x%02X，Normal 50Hz，±8G，OSR1/2=8；轴映射=%s。\n",
                  BSP::Qmc5883::TypeName(), BSP::Qmc5883::Address(),
                  BOARD_AXIS_MAPPING_VERIFIED ? "已验证" : "待实板验证");
    BSP::Qmc5883::Diagnostics diagnostics = {};
    if (BSP::Qmc5883::GetDiagnostics(&diagnostics) && diagnostics.ctrl2Valid &&
        !diagnostics.ctrl2Matches)
    {
        Serial.printf("[地磁-提示] CTRL2写入=0x%02X、回读=0x%02X；当前V4B已用场强实测确认±8G，"
                      "该回读差异仅保留诊断。\n",
                      diagnostics.expectedCtrl2, diagnostics.ctrl2);
    }
    return true;
}

bool SysMag_Update()
{
    if (!s_started || s_sleeping)
        return false;
    if (!s_available)
    {
        const uint32_t now = millis();
        if (static_cast<int32_t>(now - s_next_recovery_ms) < 0)
            return false;
        s_next_recovery_ms = now + RECOVERY_INTERVAL_MS;
        RecoverSensor();
        return false;
    }

    const uint32_t now_us = micros();
    if (static_cast<int32_t>(now_us - s_next_poll_us) < 0)
        return false;
    ScheduleNextPoll();

    BSP::Qmc5883::Reading reading = {};
    if (!BSP::Qmc5883::Read(&reading))
    {
        MarkOffline();
        return false;
    }
    if (!reading.status.dataReady)
        return false;

    SysMagSample sample = {};
    sample.sequence = s_latest.sequence + 1;
    sample.timestamp_us = micros();
    sample.sensor_type = BSP::Qmc5883::SensorType();
    sample.address = BSP::Qmc5883::Address();
    sample.raw_x = reading.xRaw;
    sample.raw_y = reading.yRaw;
    sample.raw_z = reading.zRaw;
    sample.sensor_uT = {reading.xUt, reading.yUt, reading.zUt};
    sample.fresh = true;
    sample.overflow = reading.status.overflow;

    if (s_calibration.valid)
    {
        const SysMagCalibration::Vector3 corrected = SysMagCalibration::Apply(
            s_calibration, {sample.sensor_uT.x, sample.sensor_uT.y, sample.sensor_uT.z});
        sample.calibrated_sensor_uT = {corrected.x, corrected.y, corrected.z};
    }
    else
    {
        sample.calibrated_sensor_uT = sample.sensor_uT;
    }
    sample.body_uT = SensorToBody(sample.calibrated_sensor_uT);
    sample.field_strength_uT = Magnitude(sample.calibrated_sensor_uT);
    EvaluateQuality(sample);
    AppendCalibrationSample(sample);

    s_latest = sample;
    s_has_sample = true;
    PrintDiagnostic(sample);
    return true;
}

bool SysMag_IsAvailable()
{
    return s_started && s_available && !s_sleeping;
}

bool SysMag_GetLatest(SysMagSample *out)
{
    if (!out || !s_has_sample)
        return false;
    *out = s_latest;
    return true;
}

bool SysMag_GetStatus(SysMagServiceStatus *out)
{
    if (!out)
        return false;
    out->started = s_started;
    out->available = s_available;
    out->sleeping = s_sleeping;
    out->has_sample = s_has_sample;
    out->last_error = BSP::Qmc5883::LastError();
    BSP::Qmc5883::GetDiagnostics(&out->sensor);
    return true;
}

bool SysMag_AxisMappingVerified()
{
    return BOARD_AXIS_MAPPING_VERIFIED;
}

bool SysMag_GetCalibration(SysMagCalibration::Result *out)
{
    if (!out || !s_calibration.valid)
        return false;
    *out = s_calibration;
    return true;
}

bool SysMag_StartCalibration()
{
    FreeCalibrationBuffer();
    if (!BSP::Qmc5883::RangeConfigurationVerified())
    {
        Serial.println("[地磁-校准] 当前量程寄存器尚未验证，拒绝采集校准数据。");
        return false;
    }
    s_calibration_samples = static_cast<SysMagCalibration::Vector3 *>(
        heap_caps_malloc(sizeof(SysMagCalibration::Vector3) * CALIBRATION_CAPACITY,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_calibration_samples)
    {
        Serial.println("[地磁-错误] 无法在PSRAM分配校准样本缓冲。");
        return false;
    }
    s_calibration_count = 0;
    s_calibration_active = true;
    Serial.printf("[地磁-校准] 开始三维采集，容量=%lu帧；请缓慢覆盖所有方向。\n",
                  static_cast<unsigned long>(CALIBRATION_CAPACITY));
    return true;
}

bool SysMag_FinishCalibration(SysMagCalibration::Result *out)
{
    if (!s_calibration_active || !s_calibration_samples)
        return false;
    SysMagCalibration::Result fitted = SysMagCalibration::Fit(s_calibration_samples,
                                                               s_calibration_count);
    FreeCalibrationBuffer();
    if (out)
        *out = fitted;
    if (!fitted.valid)
    {
        Serial.printf("[地磁-校准] 校准未通过：%s，样本=%lu，覆盖=%u/26，跨度=[%.1f,%.1f,%.1f]uT，RMS=%.2f%%。\n",
                      SysMagCalibration::FailureName(fitted.failure),
                      static_cast<unsigned long>(fitted.sample_count),
                      fitted.coverage_bins,
                      fitted.span_uT.x, fitted.span_uT.y, fitted.span_uT.z,
                      fitted.residual_rms_ratio * 100.0f);
        return false;
    }

    s_calibration = fitted;
    ++s_calibration_version;
    if (s_calibration_version == 0)
        s_calibration_version = 1;
    if (!SaveCalibration(s_calibration))
        return false;
    s_previous_field_strength_uT = 0.0f;
    Serial.printf("[地磁-校准] 校准完成：版本=%u，偏置=[%.2f,%.2f,%.2f]uT，场强=%.2fuT，覆盖=%u/26，RMS=%.2f%%。\n",
                  s_calibration_version,
                  fitted.bias_uT.x, fitted.bias_uT.y, fitted.bias_uT.z,
                  fitted.reference_field_uT, fitted.coverage_bins,
                  fitted.residual_rms_ratio * 100.0f);
    return true;
}

void SysMag_CancelCalibration()
{
    if (s_calibration_active)
        Serial.println("[地磁-校准] 已取消本轮采集，原校准保持不变。");
    FreeCalibrationBuffer();
}

bool SysMag_GetCalibrationProgress(SysMagCalibrationProgress *out)
{
    if (!out)
        return false;
    *out = {};
    out->active = s_calibration_active;
    out->sample_count = s_calibration_count;
    out->capacity = CALIBRATION_CAPACITY;
    if (!s_calibration_active || !s_calibration_samples || s_calibration_count == 0)
        return true;

    SysMagCalibration::Vector3 minimum = s_calibration_samples[0];
    SysMagCalibration::Vector3 maximum = s_calibration_samples[0];
    for (uint32_t index = 1; index < s_calibration_count; ++index)
    {
        const SysMagCalibration::Vector3 &sample = s_calibration_samples[index];
        minimum.x = fminf(minimum.x, sample.x); minimum.y = fminf(minimum.y, sample.y); minimum.z = fminf(minimum.z, sample.z);
        maximum.x = fmaxf(maximum.x, sample.x); maximum.y = fmaxf(maximum.y, sample.y); maximum.z = fmaxf(maximum.z, sample.z);
    }
    out->span_uT = {maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z};
    return true;
}

void SysMag_SetDiagnosticLogging(bool enabled)
{
    s_diagnostic_logging = enabled;
    s_last_diagnostic_ms = 0;
    Serial.printf("[地磁] 限频数据日志已%s。\n", enabled ? "开启" : "关闭");
}

void SysMag_Sleep()
{
    if (!s_started || s_sleeping)
        return;
    SysMag_CancelCalibration();
    s_sleeping = true;
    if (s_available && !BSP::Qmc5883::PowerDown())
        MarkOffline();
}

bool SysMag_Wakeup()
{
    if (!s_started)
        return false;
    s_sleeping = false;
    if (!s_available)
    {
        s_next_recovery_ms = millis();
        return false;
    }
    if (!BSP::Qmc5883::Wakeup())
    {
        MarkOffline();
        return false;
    }
    s_previous_field_strength_uT = 0.0f;
    ScheduleNextPoll();
    return true;
}
