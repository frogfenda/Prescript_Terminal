/*
【模块职责】QMC5883 独立系统服务。它是磁力计唯一采样者，提供原始/校准磁场缓存、质量状态、
校准会话、持久化和休眠恢复；指南针、调试页、姿态融合均只是消费者。
【坐标合同】sensor_* 保留芯片坐标；body_* 使用V4B固定安装映射。当前实板轴向未验证时
AxisMappingVerified()返回false，消费者不得把 body_* 用于正式航向或姿态纠正。
【线程约束】Init/Update/Sleep/Wakeup/校准接口均只能由Arduino主任务调用。
*/
#pragma once

#include <stdint.h>

#include "bsp/bsp_mag_qmc5883.h"
#include "sys/sys_mag_calibration.h"

enum SysMagDisturbance : uint32_t
{
    SYS_MAG_DISTURBANCE_NONE = 0,
    SYS_MAG_DISTURBANCE_OVERFLOW = 1U << 0,
    SYS_MAG_DISTURBANCE_FIELD_STRENGTH = 1U << 1,
    SYS_MAG_DISTURBANCE_FIELD_STEP = 1U << 2,
    SYS_MAG_DISTURBANCE_NOT_CALIBRATED = 1U << 3,
    SYS_MAG_DISTURBANCE_AXIS_UNVERIFIED = 1U << 4,
    SYS_MAG_DISTURBANCE_RANGE_UNVERIFIED = 1U << 5,
};

struct SysMagVector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr SysMagVector3() = default;
    constexpr SysMagVector3(float x_value, float y_value, float z_value)
        : x(x_value), y(y_value), z(z_value) {}
};

struct SysMagSample
{
    uint32_t sequence = 0;
    uint32_t timestamp_us = 0;
    BSP::Qmc5883::Type sensor_type = BSP::Qmc5883::Type::None;
    uint8_t address = 0;

    int16_t raw_x = 0;
    int16_t raw_y = 0;
    int16_t raw_z = 0;
    SysMagVector3 sensor_uT;
    SysMagVector3 calibrated_sensor_uT;
    SysMagVector3 body_uT;
    float field_strength_uT = 0.0f;

    bool fresh = false;
    bool overflow = false;
    bool calibrated = false;
    bool axis_mapping_verified = false;
    bool range_configuration_verified = false;
    bool disturbed = true;
    bool fusion_usable = false;
    float confidence = 0.0f;
    uint32_t disturbance_reasons = SYS_MAG_DISTURBANCE_NOT_CALIBRATED |
                                   SYS_MAG_DISTURBANCE_AXIS_UNVERIFIED;
    uint16_t calibration_version = 0;
};

struct SysMagCalibrationProgress
{
    bool active = false;
    uint32_t sample_count = 0;
    uint32_t capacity = 0;
    SysMagVector3 span_uT;
};

/** 独立地磁服务和BSP最近一次I2C事务的只读状态，供诊断页与故障日志使用。 */
struct SysMagServiceStatus
{
    bool started = false;
    bool available = false;
    bool sleeping = false;
    bool has_sample = false;
    BSP::Qmc5883::Error last_error = BSP::Qmc5883::Error::NotInitialized;
    BSP::Qmc5883::Diagnostics sensor;
};

bool SysMag_Init();
bool SysMag_Update();
bool SysMag_IsAvailable();
bool SysMag_GetLatest(SysMagSample *out);
bool SysMag_GetStatus(SysMagServiceStatus *out);

/** 当前V4B板级轴映射是否已经用实板三轴旋转验证；false时融合必须保持关闭。 */
bool SysMag_AxisMappingVerified();

/** 复制最近一次载入或拟合成功的设备校准；没有有效校准时返回false。 */
bool SysMag_GetCalibration(SysMagCalibration::Result *out);

/**
 * 在PSRAM分配固定样本缓冲并开始收集。采样由SysMag_Update自动推进，调用者不直接传入磁场值。
 * Finish会执行椭球拟合，成功时立即替换运行校准并写入LittleFS。
 */
bool SysMag_StartCalibration();
bool SysMag_FinishCalibration(SysMagCalibration::Result *out);
void SysMag_CancelCalibration();
bool SysMag_GetCalibrationProgress(SysMagCalibrationProgress *out);

/** 调试页进入时开启500ms限频数据日志；退出时关闭，正常固件不会持续刷串口。 */
void SysMag_SetDiagnosticLogging(bool enabled);

void SysMag_Sleep();
bool SysMag_Wakeup();
