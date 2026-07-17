/*
【模块职责】实现系统级 LSM6DSL 采样所有权、缓存和恢复策略。
【总线约束】PCF8563、TM6605、QMC5883 与 IMU 共用 Wire1；正常采样只做一次 16 字节连续读取，
不额外轮询 STATUS_REG，并把轮询周期限制为 9 ms，避免主循环空转时占满 100 kHz I2C。
【恢复策略】高频路径不打印每次失败。一次真实总线失败会把服务置为离线，之后每秒至多恢复一次；
恢复成功和离线状态转换才输出串口信息。
*/
#include "sys/sys_motion.h"

#include <Wire.h>

#include "bsp/bsp_imu_lsm6dsl.h"

namespace
{
    static constexpr uint32_t POLL_INTERVAL_US = 9000;
    static constexpr uint32_t RECOVERY_INTERVAL_MS = 1000;

    bool s_started = false;
    bool s_available = false;
    bool s_sleeping = false;
    bool s_has_sample = false;
    uint32_t s_next_poll_us = 0;
    uint32_t s_next_recovery_ms = 0;
    SysMotionSample s_latest = {};

    BSP::Lsm6dsl::Config MotionConfig()
    {
        BSP::Lsm6dsl::Config config = {};
        config.accelRate = BSP::Lsm6dsl::OutputDataRate::Hz104;
        config.gyroRate = BSP::Lsm6dsl::OutputDataRate::Hz104;
        // 换武器动作实测打满 ±4 g 且达到约 904 dps，扩大两类量程以保留动作波形和个体差异余量。
        config.accelRange = BSP::Lsm6dsl::AccelRange::G8;
        config.gyroRange = BSP::Lsm6dsl::GyroRange::Dps2000;
        return config;
    }

    void ScheduleNextPoll()
    {
        s_next_poll_us = micros() + POLL_INTERVAL_US;
    }

    /**
     * 优先复用 BSP 已保存的地址和配置做软复位；只有驱动从未找到设备时才重新 Begin。
     * 这样短暂总线错误不会无条件重建 Wire1，同时仍能处理 IMU 晚上电或插接不稳。
     */
    bool RecoverSensor()
    {
        bool ok = false;
        if (BSP::Lsm6dsl::Address() != 0)
        {
            // 曾经在线说明驱动持有完整状态；软复位失败后允许 Begin 重建一次共享 Wire1 总线。
            ok = BSP::Lsm6dsl::Reset();
            if (!ok)
                ok = BSP::Lsm6dsl::Begin(Wire1, BSP::Lsm6dsl::DEFAULT_ADDRESS, MotionConfig());
        }
        else
        {
            /*
             * 开机从未发现 IMU 时只在现有 Wire1 上检查 WHO_AM_I。
             * 不能每秒调用 Begin() 重建共享总线，否则缺件主板会周期性干扰 RTC 和 TM6605。
             */
            if (BSP::Lsm6dsl::IsPresent(BSP::Lsm6dsl::DEFAULT_ADDRESS))
                ok = BSP::Lsm6dsl::Begin(Wire1, BSP::Lsm6dsl::DEFAULT_ADDRESS, MotionConfig());
        }

        if (!ok)
            return false;

        s_available = true;
        ScheduleNextPoll();
        Serial.println("[运动] LSM6DSL 通信已恢复。");
        return true;
    }

    void MarkOffline()
    {
        if (s_available)
        {
            Serial.printf("[运动-警告] LSM6DSL 采样失败，稍后自动恢复：错误码=%u。\n",
                          (unsigned)BSP::Lsm6dsl::LastError());
        }
        s_available = false;
        s_next_recovery_ms = millis() + RECOVERY_INTERVAL_MS;
    }
}

bool SysMotion_Init()
{
    s_started = true;
    s_available = false;
    s_sleeping = false;
    s_has_sample = false;
    s_next_poll_us = 0;
    s_next_recovery_ms = 0;
    s_latest = {};

    if (!BSP::Lsm6dsl::Begin(Wire1, BSP::Lsm6dsl::DEFAULT_ADDRESS, MotionConfig()))
    {
        Serial.printf("[运动-警告] LSM6DSL 初始化失败，运动功能暂不可用：错误码=%u。\n",
                      (unsigned)BSP::Lsm6dsl::LastError());
        s_next_recovery_ms = millis() + RECOVERY_INTERVAL_MS;
        return false;
    }

    s_available = true;
    ScheduleNextPoll();
    Serial.println("[运动] LSM6DSL 已初始化：104 Hz，±8 g，±2000 dps。");
    return true;
}

bool SysMotion_IsAvailable()
{
    return s_started && s_available && !s_sleeping;
}

bool SysMotion_Update()
{
    if (!s_started || s_sleeping)
        return false;

    if (!s_available)
    {
        const uint32_t now_ms = millis();
        if ((int32_t)(now_ms - s_next_recovery_ms) < 0)
            return false;

        s_next_recovery_ms = now_ms + RECOVERY_INTERVAL_MS;
        RecoverSensor();
        return false;
    }

    const uint32_t now_us = micros();
    if ((int32_t)(now_us - s_next_poll_us) < 0)
        return false;
    ScheduleNextPoll();

    BSP::Lsm6dsl::Reading reading = {};
    if (!BSP::Lsm6dsl::Read(&reading))
    {
        MarkOffline();
        return false;
    }

    // 轮询可能略早于 104 Hz 数据边沿；没有任何新六轴数据时不推进 sequence，避免上层重复处理。
    if (!reading.ready.accel && !reading.ready.gyro)
        return false;

    SysMotionSample sample = {};
    sample.sequence = s_latest.sequence + 1;
    sample.timestamp_us = micros();
    sample.accel_fresh = reading.ready.accel;
    sample.gyro_fresh = reading.ready.gyro;
    sample.temperature_fresh = reading.ready.temperature;

    sample.ax_raw = reading.axRaw;
    sample.ay_raw = reading.ayRaw;
    sample.az_raw = reading.azRaw;
    sample.gx_raw = reading.gxRaw;
    sample.gy_raw = reading.gyRaw;
    sample.gz_raw = reading.gzRaw;
    sample.temperature_raw = reading.temperatureRaw;

    sample.imu.axG = reading.axG;
    sample.imu.ayG = reading.ayG;
    sample.imu.azG = reading.azG;
    sample.imu.gxDps = reading.gxDps;
    sample.imu.gyDps = reading.gyDps;
    sample.imu.gzDps = reading.gzDps;
    sample.temperature_c = reading.temperatureC;

    s_latest = sample;
    s_has_sample = true;
    return true;
}

bool SysMotion_GetLatest(SysMotionSample *out)
{
    if (!out || !s_has_sample)
        return false;

    *out = s_latest;
    return true;
}

void SysMotion_Sleep()
{
    if (!s_started || s_sleeping)
        return;

    s_sleeping = true;
    if (s_available && !BSP::Lsm6dsl::PowerDown())
        MarkOffline();
}

bool SysMotion_Wakeup()
{
    if (!s_started)
        return false;

    s_sleeping = false;
    if (!s_available)
    {
        // 唤醒路径不阻塞重试离线设备；下一次主循环立即进入统一恢复流程。
        s_next_recovery_ms = millis();
        return false;
    }

    if (!BSP::Lsm6dsl::Wakeup())
    {
        MarkOffline();
        return false;
    }

    ScheduleNextPoll();
    return true;
}
