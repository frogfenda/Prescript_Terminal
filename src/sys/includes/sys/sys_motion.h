/*
【模块职责】系统级运动采样服务。它是正常运行时唯一允许持续读取 LSM6DSL 的模块，负责定频轮询、
最新样本缓存、共享 I2C 故障恢复以及 Light Sleep 前后的传感器功耗切换。
【能力边界】本服务暂不猜测 V4B 的物理轴向，也不判断“上滚、下滚、换武器”等动作；这些策略必须在
实板采集完成后建立在本服务之上，避免多个 APP 各自访问 BSP 或维护重复阈值。
【调用关系】setup() 调用 SysMotion_Init()，主循环调用 SysMotion_Update()；APP 和算法只能读取缓存样本。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_pose_solver.h"

/**
 * 一份系统运动样本。
 * imu 字段可直接交给现有 MahonySolver；raw 字段保留给实板标定、饱和判断和诊断采集。
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

    SysPose::ImuSample imu;
    float temperature_c = 0.0f;
};

/**
 * 初始化 LSM6DSL 和采样状态。当前统一使用 104 Hz、±8 g、±2000 dps；实板动作采集确认
 * 换武器手势会打满 ±4 g，并接近 ±1000 dps。初始化失败不会阻止系统启动，Update 会低频重试。
 */
bool SysMotion_Init();

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
