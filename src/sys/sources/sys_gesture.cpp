/*
【模块职责】实现基于 LSM6DSL 实板采样标定的离散手势状态机。
【识别原则】滚动不是单点峰值，而是 gy 主脉冲后紧跟反向回摆；换武器由 gz 高速旋转、
轴向主导关系和加速度幅值共同确认。这样可以避免正常手持中的单次冲击直接变成菜单输入。
【重要约束】所有阈值单位均为 g、dps、us，输入只能来自 SysMotion 的物理量缓存。
*/
#include "sys/sys_gesture.h"

#include <math.h>

#include "sys/sys_motion.h"

namespace
{
    /*
     * 参数来自 V4B 实板标签 7/8/9/A 的离线回放：
     * - 滚动主峰约 414~753 dps，反向回摆约 180~387 dps，完整相位约 110~260ms；
     * - 换武器新量程实测 gz 峰值约 1044 dps，正常手持 gz 峰值约 681 dps；
     * - 单纯正常手持的 gy 偶尔很高，因此必须同时满足相位顺序和主轴占优；
     * - 上摇保持 335/385/170 dps；下摇经实机继续调整为 270/350/130 dps。用户确认单纯降低
     *   幅度后仍不够灵敏，说明限制来自轴向和相位结构，因此负向起始/整段主轴比分别放宽为
     *   1.4/1.6，负向窗口延长到 340ms；上摇结构参数保持不变。
     * 这些常量只属于识别算法，不放入系统公共常量，避免其他模块依赖尚需实板微调的策略值。
     */
    constexpr float SCROLL_UP_ARM_DPS = 335.0f;
    constexpr float SCROLL_UP_PRIMARY_PEAK_DPS = 385.0f;
    constexpr float SCROLL_UP_RETURN_PEAK_DPS = 170.0f;
    constexpr float SCROLL_DOWN_ARM_DPS = 270.0f;
    constexpr float SCROLL_DOWN_PRIMARY_PEAK_DPS = 350.0f;
    constexpr float SCROLL_DOWN_RETURN_PEAK_DPS = 130.0f;
    constexpr float SCROLL_UP_AXIS_DOMINANCE = 1.7f;
    constexpr float SCROLL_DOWN_AXIS_DOMINANCE = 1.4f;
    constexpr float SCROLL_DOWN_WHOLE_PHASE_DOMINANCE = 1.6f;
    constexpr float SCROLL_CANCEL_GZ_DPS = 450.0f;
    constexpr uint32_t SCROLL_MIN_PHASE_US = 60000;
    constexpr uint32_t SCROLL_UP_MAX_PHASE_US = 280000;
    constexpr uint32_t SCROLL_DOWN_MAX_PHASE_US = 340000;
    constexpr uint32_t SCROLL_COOLDOWN_US = 400000;

    constexpr float WEAPON_GZ_PEAK_DPS = 750.0f;
    constexpr float WEAPON_GZ_TO_GY_RATIO = 1.25f;
    constexpr float WEAPON_GZ_TO_GX_RATIO = 1.50f;
    constexpr float WEAPON_ACCEL_MAG_G = 1.40f;
    constexpr uint32_t WEAPON_COOLDOWN_US = 800000;

    // 超过该间隔说明传感器休眠、离线恢复或主循环长时间阻塞；半截动作不能跨越这种断点。
    constexpr uint32_t SAMPLE_DISCONTINUITY_US = 100000;
    constexpr uint8_t EVENT_QUEUE_CAPACITY = 4;

    struct ScrollTracker
    {
        bool active = false;
        int8_t direction = 0;
        uint32_t started_us = 0;
        float primary_peak_dps = 0.0f;
        float return_peak_dps = 0.0f;
        float cross_axis_peak_dps = 0.0f;
    };

    ScrollTracker s_scroll = {};
    uint32_t s_last_sequence = 0;
    uint32_t s_last_sample_us = 0;
    uint32_t s_cooldown_until_us = 0;
    SysGestureEvent s_queue[EVENT_QUEUE_CAPACITY] = {};
    uint8_t s_queue_head = 0;
    uint8_t s_queue_count = 0;

    bool DeadlinePending(uint32_t now, uint32_t deadline)
    {
        return deadline != 0 && (int32_t)(now - deadline) < 0;
    }

    void ResetTracking()
    {
        s_scroll = {};
    }

    void PushEvent(SysGestureType type, uint32_t timestamp_us, float strength_dps, int8_t direction)
    {
        /*
         * 事件代表即时交互，队列满时旧事件已经失去时效，丢弃最旧一条比阻塞采样或丢掉新动作更合理。
         * 正常冷却时间远大于 AppManager 一帧，队列满只会发生在主循环被其他模块长时间阻塞时。
         */
        if (s_queue_count == EVENT_QUEUE_CAPACITY)
        {
            s_queue_head = (uint8_t)((s_queue_head + 1) % EVENT_QUEUE_CAPACITY);
            --s_queue_count;
        }

        const uint8_t tail = (uint8_t)((s_queue_head + s_queue_count) % EVENT_QUEUE_CAPACITY);
        s_queue[tail].type = type;
        s_queue[tail].timestamp_us = timestamp_us;
        s_queue[tail].strength_dps = strength_dps;
        s_queue[tail].direction = direction;
        ++s_queue_count;
    }

    bool DetectWeaponChange(const SysMotionSample &sample)
    {
        const float abs_gx = fabsf(sample.imu.gxDps);
        const float abs_gy = fabsf(sample.imu.gyDps);
        const float abs_gz = fabsf(sample.imu.gzDps);
        const float accel_mag = sqrtf(sample.imu.axG * sample.imu.axG +
                                      sample.imu.ayG * sample.imu.ayG +
                                      sample.imu.azG * sample.imu.azG);

        if (abs_gz < WEAPON_GZ_PEAK_DPS ||
            abs_gz < abs_gy * WEAPON_GZ_TO_GY_RATIO ||
            abs_gz < abs_gx * WEAPON_GZ_TO_GX_RATIO ||
            accel_mag < WEAPON_ACCEL_MAG_G)
        {
            return false;
        }

        const int8_t direction = sample.imu.gzDps >= 0.0f ? 1 : -1;
        PushEvent(SysGestureType::WeaponChange, sample.timestamp_us, abs_gz, direction);
        ResetTracking();
        s_cooldown_until_us = sample.timestamp_us + WEAPON_COOLDOWN_US;
        return true;
    }

    void UpdateScroll(const SysMotionSample &sample)
    {
        const uint32_t now = sample.timestamp_us;
        const float abs_gx = fabsf(sample.imu.gxDps);
        const float abs_gy = fabsf(sample.imu.gyDps);
        const float abs_gz = fabsf(sample.imu.gzDps);

        if (!s_scroll.active)
        {
            const bool is_down = sample.imu.gyDps < 0.0f;
            const float arm_dps = is_down ? SCROLL_DOWN_ARM_DPS : SCROLL_UP_ARM_DPS;
            const float axis_dominance = is_down ? SCROLL_DOWN_AXIS_DOMINANCE
                                                 : SCROLL_UP_AXIS_DOMINANCE;

            // 起始样本必须由 gy 明显主导，防止扭转设备或换武器动作误启动滚动跟踪。
            if (abs_gy >= arm_dps &&
                abs_gy >= abs_gx * axis_dominance &&
                abs_gy >= abs_gz * axis_dominance)
            {
                s_scroll.active = true;
                s_scroll.direction = sample.imu.gyDps >= 0.0f ? 1 : -1;
                s_scroll.started_us = now;
                s_scroll.primary_peak_dps = abs_gy;
                s_scroll.return_peak_dps = 0.0f;
                s_scroll.cross_axis_peak_dps = abs_gx;
            }
            return;
        }

        const uint32_t elapsed_us = now - s_scroll.started_us;
        const uint32_t max_phase_us = s_scroll.direction < 0 ? SCROLL_DOWN_MAX_PHASE_US
                                                            : SCROLL_UP_MAX_PHASE_US;
        if (elapsed_us > max_phase_us || abs_gz > SCROLL_CANCEL_GZ_DPS)
        {
            ResetTracking();
            return;
        }

        if (sample.imu.gyDps * s_scroll.direction >= 0.0f)
            s_scroll.primary_peak_dps = fmaxf(s_scroll.primary_peak_dps, abs_gy);
        else
            s_scroll.return_peak_dps = fmaxf(s_scroll.return_peak_dps, abs_gy);
        s_scroll.cross_axis_peak_dps = fmaxf(s_scroll.cross_axis_peak_dps, abs_gx);

        const bool is_down = s_scroll.direction < 0;
        const float primary_threshold = is_down ? SCROLL_DOWN_PRIMARY_PEAK_DPS
                                                : SCROLL_UP_PRIMARY_PEAK_DPS;
        const float return_threshold = is_down ? SCROLL_DOWN_RETURN_PEAK_DPS
                                               : SCROLL_UP_RETURN_PEAK_DPS;
        /*
         * 下摇阈值更低，因此仍检查整个识别窗口，而不只检查起始样本。实机继续验证发现 2.0 倍
         * 会把带少量横向分量的自然下摇拒绝，放宽到 1.6 后仍能挡住大多数非 gy 主导动作。
         */
        const bool whole_phase_axis_valid = !is_down ||
                                            s_scroll.primary_peak_dps >=
                                                s_scroll.cross_axis_peak_dps * SCROLL_DOWN_WHOLE_PHASE_DOMINANCE;

        if (elapsed_us < SCROLL_MIN_PHASE_US ||
            s_scroll.primary_peak_dps < primary_threshold ||
            s_scroll.return_peak_dps < return_threshold ||
            !whole_phase_axis_valid)
        {
            return;
        }

        const SysGestureType type = s_scroll.direction > 0
                                        ? SysGestureType::ScrollUp
                                        : SysGestureType::ScrollDown;
        PushEvent(type, now, s_scroll.primary_peak_dps, s_scroll.direction);
        ResetTracking();
        s_cooldown_until_us = now + SCROLL_COOLDOWN_US;
    }
}

void SysGesture_Init()
{
    SysGesture_Reset();
}

void SysGesture_Update()
{
    SysMotionSample sample = {};
    if (!SysMotion_GetLatest(&sample) || sample.sequence == s_last_sequence)
        return;

    s_last_sequence = sample.sequence;

    // SysMotion 在加速度或陀螺仪任一数据就绪时都会推进 sequence；离散手势只消费新的陀螺仪帧。
    if (!sample.gyro_fresh)
        return;

    if (s_last_sample_us != 0 && sample.timestamp_us - s_last_sample_us > SAMPLE_DISCONTINUITY_US)
        ResetTracking();
    s_last_sample_us = sample.timestamp_us;

    if (DeadlinePending(sample.timestamp_us, s_cooldown_until_us))
        return;
    // 到期后清零，避免 micros() 再经过半圈回绕时把一个陈旧截止值重新解释为未来时间。
    s_cooldown_until_us = 0;

    // 换武器的幅度和误操作风险都更高，必须先判定并阻止同一波形继续进入滚动识别。
    if (DetectWeaponChange(sample))
        return;

    UpdateScroll(sample);
}

bool SysGesture_PopEvent(SysGestureEvent *out)
{
    if (!out || s_queue_count == 0)
        return false;

    *out = s_queue[s_queue_head];
    s_queue_head = (uint8_t)((s_queue_head + 1) % EVENT_QUEUE_CAPACITY);
    --s_queue_count;
    return true;
}

void SysGesture_Reset()
{
    ResetTracking();
    s_last_sequence = 0;
    s_last_sample_us = 0;
    s_cooldown_until_us = 0;
    s_queue_head = 0;
    s_queue_count = 0;
    for (auto &event : s_queue)
        event = {};
}
