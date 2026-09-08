/*
【模块职责】实现系统级 LSM6DSV 采样所有权、缓存和恢复策略。
【总线约束】PCF8563、TM6605、MMC5603NJ与IMU共用Wire1；先读取状态，确认有新数据后才连续读取
14 字节输出寄存器。120 Hz 节拍使用绝对相位推进，避免主循环毫秒级粒度把 8.33 ms 累积成 9 ms。
【恢复策略】高频路径不打印每次失败。一次真实总线失败会把服务置为离线，之后每秒至多恢复一次；
恢复成功和离线状态转换才输出串口信息。
*/
#include "sys/sys_motion.h"

#include <new>
#include <Wire.h>

#include <esp_heap_caps.h>

#include "bsp/bsp_imu_lsm6dsv.h"

namespace
{
    // 120 Hz 的整数微秒近似值。采用 8333 而不是 8300，长期频率误差低于 0.01%。
    static constexpr uint32_t POLL_INTERVAL_US = 8333;
    static constexpr uint32_t NOT_READY_RETRY_US = 300;
    static constexpr uint32_t RECOVERY_INTERVAL_MS = 1000;
    // 120Hz 下约覆盖 333ms。主循环短时阻塞时保留完整动作相位，溢出则由上层安全复位。
    static constexpr uint8_t PENDING_SAMPLE_CAPACITY = 40;
    static constexpr SysMotionAcquisitionConfig ACQUISITION_CONFIG = {120, 16, 2000};

    bool s_started = false;
    bool s_available = false;
    bool s_sleeping = false;
    bool s_has_sample = false;
    uint32_t s_next_poll_us = 0;
    uint32_t s_next_recovery_ms = 0;
    SysMotionSample s_latest = {};
    /*
     * 40帧待消费环用于吸收主循环短暂停顿，约占3.1 KiB。它不参与DMA或ISR，放入PSRAM可把
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
     * 2026-09-07 新板六面静态数据唯一确定安装矩阵：
     * BodyX=+SensorY、BodyY=-SensorX、BodyZ=+SensorZ。三组半轴跨度分别约为
     * 0.9991/0.9983/1.0007 g，且该矩阵行列式为 +1，是右手坐标变换。
     * 加速度与角速度必须使用同一旋转矩阵；sensor_imu 继续保留芯片原生坐标，仅供采集诊断。
     */
    SysPose::ImuSample SensorToBody(const SysPose::ImuSample &sensor)
    {
        SysPose::ImuSample body = {};
        body.axG = sensor.ayG;
        body.ayG = -sensor.axG;
        body.azG = sensor.azG;
        body.gxDps = sensor.gyDps;
        body.gyDps = -sensor.gxDps;
        body.gzDps = sensor.gzDps;
        return body;
    }

    BSP::Lsm6dsv::Config MotionConfig()
    {
        BSP::Lsm6dsv::Config config = {};
        config.accelRate = BSP::Lsm6dsv::OutputDataRate::Hz120;
        config.gyroRate = BSP::Lsm6dsv::OutputDataRate::Hz120;
        /*
         * 首批双蛇杖横斩/竖斩在 ±8 g 下已出现单轴 raw 削顶。全系统消费者都读取 BSP
         * 换算后的物理量，因此统一升到 ±16 g 可以保留完整冲击波形，不需要按比例修改
         * 滚动、业力、换武器或海的 g/dps 阈值；代价是加速度分辨率减半。
         */
        config.accelRange = BSP::Lsm6dsv::AccelRange::G16;
        config.gyroRange = BSP::Lsm6dsv::GyroRange::Dps2000;
        return config;
    }

    /** 从当前时刻建立新的采样相位，只用于初始化、恢复和唤醒。 */
    void ResetPollSchedule()
    {
        s_next_poll_us = micros() + POLL_INTERVAL_US;
    }

    /**
     * 沿既有相位推进到 now_us 之后，而不是从当前时刻重新计时。这样即使主循环只能在整数毫秒附近
     * 运行，也会自然形成 8/9 ms 交替节拍，不会把每帧迟到的零点几毫秒永久累加进下一帧。
     */
    void AdvancePollSchedule(uint32_t now_us)
    {
        const uint32_t elapsed_us = now_us - s_next_poll_us;
        const uint32_t periods = elapsed_us / POLL_INTERVAL_US + 1;
        s_next_poll_us += periods * POLL_INTERVAL_US;
    }

    /**
     * 优先复用 BSP 已保存的地址和配置做软复位；只有驱动从未找到设备时才重新 Begin。
     * 这样短暂总线错误不会无条件重建 Wire1，同时仍能处理 IMU 晚上电或插接不稳。
     */
    bool RecoverSensor()
    {
        bool ok = false;
        if (BSP::Lsm6dsv::Address() != 0)
        {
            // 曾经在线说明驱动持有完整状态；软复位失败后允许 Begin 重建一次共享 Wire1 总线。
            ok = BSP::Lsm6dsv::Reset();
            if (!ok)
                ok = BSP::Lsm6dsv::Begin(Wire1, BSP::Lsm6dsv::DEFAULT_ADDRESS, MotionConfig());
        }
        else
        {
            /*
             * 开机从未发现 IMU 时只在现有 Wire1 上检查 WHO_AM_I。
             * 不能每秒调用 Begin() 重建共享总线，否则缺件主板会周期性干扰 RTC 和 TM6605。
             */
            if (BSP::Lsm6dsv::IsPresent(BSP::Lsm6dsv::DEFAULT_ADDRESS))
                ok = BSP::Lsm6dsv::Begin(Wire1, BSP::Lsm6dsv::DEFAULT_ADDRESS, MotionConfig());
        }

        if (!ok)
            return false;

        s_available = true;
        ResetPollSchedule();
        Serial.println("[运动] LSM6DSV 通信已恢复。");
        return true;
    }

    void MarkOffline()
    {
        if (s_available)
        {
            Serial.printf("[运动-警告] LSM6DSV 采样失败，稍后自动恢复：错误码=%u。\n",
                          (unsigned)BSP::Lsm6dsv::LastError());
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

    if (!BSP::Lsm6dsv::Begin(Wire1, BSP::Lsm6dsv::DEFAULT_ADDRESS, MotionConfig()))
    {
        Serial.printf("[运动-警告] LSM6DSV 初始化失败，运动功能暂不可用：错误码=%u。\n",
                      (unsigned)BSP::Lsm6dsv::LastError());
        s_next_recovery_ms = millis() + RECOVERY_INTERVAL_MS;
        return false;
    }

    s_available = true;
    ResetPollSchedule();
    Serial.println("[运动] LSM6DSV 已初始化：120 Hz，±16 g，±2000 dps。");
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
    AdvancePollSchedule(now_us);

    BSP::Lsm6dsv::Reading reading = {};
    if (!BSP::Lsm6dsv::Read(&reading))
    {
        MarkOffline();
        return false;
    }

    // 轮询可能略早于 120 Hz 数据边沿；短重试避免一次相位擦边直接损失完整的 8.3 ms 样本周期。
    if (!reading.ready.accel && !reading.ready.gyro)
    {
        s_next_poll_us = micros() + NOT_READY_RETRY_US;
        return false;
    }

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
    if (s_available && !BSP::Lsm6dsv::PowerDown())
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

    if (!BSP::Lsm6dsv::Wakeup())
    {
        MarkOffline();
        return false;
    }

    ResetPollSchedule();
    return true;
}
