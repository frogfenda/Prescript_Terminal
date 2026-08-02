/*
【模块职责】实现系统级 LSM6DSL 采样所有权、缓存和恢复策略。
【总线约束】PCF8563、TM6605、QMC5883 与 IMU 共用 Wire1；正常采样只做一次 16 字节连续读取，
不额外轮询 STATUS_REG，并把轮询周期限制为 9 ms，避免主循环空转时占满 100 kHz I2C。
【恢复策略】高频路径不打印每次失败。一次真实总线失败会把服务置为离线，之后每秒至多恢复一次；
恢复成功和离线状态转换才输出串口信息。
*/
#include "sys/sys_motion.h"

#include <new>
#include <Wire.h>

#include <esp_heap_caps.h>

#include "bsp/bsp_imu_lsm6dsl.h"

namespace
{
    static constexpr uint32_t POLL_INTERVAL_US = 9000;
    static constexpr uint32_t RECOVERY_INTERVAL_MS = 1000;
    // 104Hz 下约覆盖 300ms。主循环短时阻塞时保留完整动作相位，溢出则由上层安全复位。
    static constexpr uint8_t PENDING_SAMPLE_CAPACITY = 32;
    static constexpr SysMotionAcquisitionConfig ACQUISITION_CONFIG = {104, 16, 2000};

    bool s_started = false;
    bool s_available = false;
    bool s_sleeping = false;
    bool s_has_sample = false;
    uint32_t s_next_poll_us = 0;
    uint32_t s_next_recovery_ms = 0;
    SysMotionSample s_latest = {};
    /*
     * 32帧待消费环用于吸收主循环短暂停顿，约占2.5 KiB。它不参与DMA或ISR，放入PSRAM可把
     * 内部DRAM留给BLE、WiFi和FreeRTOS任务栈；初始化后固定地址，采样路径不会重复分配。
     * 若PSRAM异常，退化为一帧内部缓冲并标记溢出，保证普通姿态消费者仍可工作。
     */
    SysMotionSample *s_pending = nullptr;
    SysMotionSample s_pending_fallback = {};
    uint8_t s_pending_capacity = 0;
    uint8_t s_pending_head = 0;
    uint8_t s_pending_count = 0;
    bool s_pending_overflow = false;

    void EnsurePendingStorage()
    {
        if (s_pending)
            return;

        void *storage = heap_caps_malloc(sizeof(SysMotionSample) * PENDING_SAMPLE_CAPACITY,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage)
        {
            s_pending = &s_pending_fallback;
            s_pending_capacity = 1;
            Serial.println("[运动] 待消费采样环申请PSRAM失败，已退化为单帧缓冲。");
            return;
        }

        s_pending = static_cast<SysMotionSample *>(storage);
        s_pending_capacity = PENDING_SAMPLE_CAPACITY;
        for (uint8_t index = 0; index < s_pending_capacity; ++index)
            new (&s_pending[index]) SysMotionSample();
    }

    void ClearPendingSamples()
    {
        EnsurePendingStorage();
        s_pending_head = 0;
        s_pending_count = 0;
        s_pending_overflow = false;
        for (uint8_t index = 0; index < s_pending_capacity; ++index)
            s_pending[index] = {};
    }

    void EnqueuePendingSample(const SysMotionSample &sample)
    {
        if (s_pending_count >= s_pending_capacity)
        {
            // 丢弃最旧帧会破坏连续动作相位，因此必须通知识别器重新锚定，而不是静默拼接。
            s_pending_head = (uint8_t)((s_pending_head + 1) % s_pending_capacity);
            --s_pending_count;
            s_pending_overflow = true;
        }
        const uint8_t target = (uint8_t)((s_pending_head + s_pending_count) % s_pending_capacity);
        s_pending[target] = sample;
        ++s_pending_count;
    }

    /**
     * 把LSM6DSL传感器坐标转换为V4B统一机身坐标。
     * 2026-07-30六面静态标签1～6得到BodyX=-SensorY、BodyY=+SensorX、BodyZ=+SensorZ，
     * 标签20～25的三轴双方向旋转又独立验证了同一陀螺仪轴和符号。该变换是行列式+1的
     * 有符号置换，必须同时作用于加速度和角速度；这里不混入单台设备、单温度下的零偏值。
     */
    SysPose::ImuSample SensorToBody(const SysPose::ImuSample &sensor)
    {
        SysPose::ImuSample body = {};
        body.axG = -sensor.ayG;
        body.ayG = sensor.axG;
        body.azG = sensor.azG;
        body.gxDps = -sensor.gyDps;
        body.gyDps = sensor.gxDps;
        body.gzDps = sensor.gzDps;
        return body;
    }

    BSP::Lsm6dsl::Config MotionConfig()
    {
        BSP::Lsm6dsl::Config config = {};
        config.accelRate = BSP::Lsm6dsl::OutputDataRate::Hz104;
        config.gyroRate = BSP::Lsm6dsl::OutputDataRate::Hz104;
        /*
         * 首批双蛇杖横斩/竖斩在 ±8 g 下已出现单轴 raw 削顶。全系统消费者都读取 BSP
         * 换算后的物理量，因此统一升到 ±16 g 可以保留完整冲击波形，不需要按比例修改
         * 滚动、业力、换武器或海的 g/dps 阈值；代价是加速度分辨率减半。
         */
        config.accelRange = BSP::Lsm6dsl::AccelRange::G16;
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
    ClearPendingSamples();

    if (!BSP::Lsm6dsl::Begin(Wire1, BSP::Lsm6dsl::DEFAULT_ADDRESS, MotionConfig()))
    {
        Serial.printf("[运动-警告] LSM6DSL 初始化失败，运动功能暂不可用：错误码=%u。\n",
                      (unsigned)BSP::Lsm6dsl::LastError());
        s_next_recovery_ms = millis() + RECOVERY_INTERVAL_MS;
        return false;
    }

    s_available = true;
    ScheduleNextPoll();
    Serial.println("[运动] LSM6DSL 已初始化：104 Hz，±16 g，±2000 dps。");
    return true;
}

bool SysMotion_GetAcquisitionConfig(SysMotionAcquisitionConfig *out)
{
    if (!out)
        return false;
    *out = ACQUISITION_CONFIG;
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

    sample.sensor_imu.axG = reading.axG;
    sample.sensor_imu.ayG = reading.ayG;
    sample.sensor_imu.azG = reading.azG;
    sample.sensor_imu.gxDps = reading.gxDps;
    sample.sensor_imu.gyDps = reading.gyDps;
    sample.sensor_imu.gzDps = reading.gzDps;
    sample.body_imu = SensorToBody(sample.sensor_imu);
    sample.temperature_c = reading.temperatureC;

    s_latest = sample;
    s_has_sample = true;
    EnqueuePendingSample(sample);
    return true;
}

bool SysMotion_GetLatest(SysMotionSample *out)
{
    if (!out || !s_has_sample)
        return false;

    *out = s_latest;
    return true;
}

bool SysMotion_PopPending(SysMotionSample *out)
{
    if (!out || s_pending_count == 0)
        return false;
    *out = s_pending[s_pending_head];
    s_pending_head = (uint8_t)((s_pending_head + 1) % s_pending_capacity);
    --s_pending_count;
    return true;
}

bool SysMotion_ConsumePendingOverflow()
{
    const bool overflowed = s_pending_overflow;
    s_pending_overflow = false;
    return overflowed;
}

void SysMotion_Sleep()
{
    if (!s_started || s_sleeping)
        return;

    s_sleeping = true;
    ClearPendingSamples();
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
