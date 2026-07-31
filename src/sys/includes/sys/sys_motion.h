/*
【模块职责】系统级运动采样服务。它是正常运行时唯一允许持续读取 LSM6DSL 的模块，负责定频轮询、
最新样本缓存、共享 I2C 故障恢复以及 Light Sleep 前后的传感器功耗切换。
【坐标契约】BSP换算值保留为LSM6DSL传感器坐标；本服务同时按V4B六面/三轴实测安装关系生成统一
机身坐标：+X向屏幕右、+Y向机身顶部、+Z向屏幕外。这里只做固定坐标变换，不判断动作语义。
【调用关系】setup() 调用 SysMotion_Init()，主循环调用 SysMotion_Update()；APP 和算法只能读取缓存样本。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_pose_solver.h"

/**
 * 一份系统运动样本。
 * 【坐标层级】raw字段是LSM6DSL寄存器整数；sensor_imu是量程换算后的传感器原生坐标；
 * body_imu是固定安装矩阵转换后的机身右手坐标（+X屏幕右、+Y机身顶部、+Z屏幕外）。
 * 【兼容约束】Default/Karma/Sea在完成各自实机回归前继续显式读取sensor_imu；新的机身方向算法
 * 必须显式读取body_imu，不能再使用含义不清的通用imu字段。
 */
struct SysMotionSample
{
    uint32_t sequence = 0;
    uint32_t timestamp_us = 0;
    bool accel_fresh = false;
    bool gyro_fresh = false;
    bool temperature_fresh = false;

    int16_t ax_raw = 0;
    int16_t ay_raw = 0;
    int16_t az_raw = 0;
    int16_t gx_raw = 0;
    int16_t gy_raw = 0;
    int16_t gz_raw = 0;
    int16_t temperature_raw = 0;

    SysPose::ImuSample sensor_imu;
    SysPose::ImuSample body_imu;
    float temperature_c = 0.0f;
};

/**
 * SysMotion 当前固定的采集契约。脱线记录器把这些值写入 CSV 元数据，分析工具据此选择
 * raw 到物理量的换算比例，避免量程调整后继续套用旧系数。上层业务只应读取本结构用于
 * 诊断和数据标注，不应根据量程自行重新换算或访问 BSP 配置。
 */
struct SysMotionAcquisitionConfig
{
    uint16_t output_rate_hz;
    uint8_t accel_range_g;
    uint16_t gyro_range_dps;
};

/**
 * 初始化 LSM6DSL 和采样状态。当前统一使用 104 Hz、±16 g、±2000 dps；首批双蛇杖
 * 横斩/竖斩数据确认 ±8 g 已削顶。初始化失败不会阻止系统启动，Update 会低频重试。
 */
bool SysMotion_Init();

/**
 * 复制正常固件与脱线采集固件共用的采集频率和量程。返回 false 表示 out 为空；该配置是
 * 编译期契约，不要求传感器已经在线，因此错误页仍可把预期量程写入诊断信息。
 */
bool SysMotion_GetAcquisitionConfig(SysMotionAcquisitionConfig *out);

/** 返回运动传感器当前是否在线且可以采样。 */
bool SysMotion_IsAvailable();

/**
 * 按内部节拍执行至多一次 I2C 采样；只有获得至少一项新六轴数据时返回 true。
 * 必须从 Arduino 主循环调用，不能从中断、网络任务或 APP 自建任务调用。
 */
bool SysMotion_Update();

/**
 * 复制最近一次有效样本。返回 false 表示尚未取得任何样本或 out 为空。
 * 读取不会消费样本；多个算法可比较 sequence 独立判断自己是否已经处理过。
 */
bool SysMotion_GetLatest(SysMotionSample *out);

/** Light Sleep 前关闭两个测量单元；服务离线时也会停止自动恢复，避免静默唤醒期间访问 I2C。 */
void SysMotion_Sleep();

/**
 * 前台唤醒后恢复最后配置和采样节拍。返回 false 表示当前仍离线，后续 Update 会继续低频恢复。
 */
bool SysMotion_Wakeup();
