/*
【模块职责】实现基于 LSM6DSL 实板采样标定的离散手势状态机。
【识别原则】滚动由 gy 主脉冲和反向回摆确认；业力长边敲击同样检查 gz 主脉冲、反向回摆、
主轴占优和冲击加速度，不使用单点峰值。换武器只在默认上下文中使用原有 gz 高速判定。
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

    /*
     * 业力参数来自 2026-07-26 V4B 标签 C/D 的离线分相回放：
     * - A 为正 gz，B 为负 gz；有效主峰最低约 553 dps，反向回摆最低约 181 dps；
     * - 完整相位约 100~280ms，主峰相对 gx/gy 的最小整段占优比约 1.46；
     * - 首版使用 540/170dps、1.75g 后，用户实机确认仍需过重动作才能触发；第二轮灵敏度
     *   标定保留“主脉冲+反向回摆+主轴占优”三重结构，同时把幅度门槛整体下调约 20%~30%；
     * - 放宽后的静止六姿态和近静止会话离线回放仍为 0 次，既有 A/B 会话仍保持单向识别，
     *   因此优先降低动作力度，而不删除相位结构变成容易误触的单点判定；
     * - 冷却只属于业力识别器，不冻结同一页面仍需使用的滚动动作。
     * 阈值只存在于 SYS 识别实现中，App 只接收语义事件，不能重复判断原始传感器数值。
     */
    constexpr float KARMA_ARM_DPS = 170.0f;
    constexpr float KARMA_PRIMARY_PEAK_DPS = 400.0f;
    constexpr float KARMA_RETURN_PEAK_DPS = 100.0f;
    constexpr float KARMA_ARM_AXIS_DOMINANCE = 1.20f;
    constexpr float KARMA_WHOLE_PHASE_DOMINANCE = 1.20f;
    constexpr float KARMA_ACCEL_PEAK_G = 1.25f;
    constexpr uint32_t KARMA_MIN_PHASE_US = 60000;
    constexpr uint32_t KARMA_MAX_PHASE_US = 450000;
    constexpr uint32_t KARMA_COOLDOWN_US = 300000;

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

    struct KarmaTracker
    {
        bool active = false;
        int8_t direction = 0;
        uint32_t started_us = 0;
        float primary_peak_dps = 0.0f;
        float return_peak_dps = 0.0f;
        float cross_axis_peak_dps = 0.0f;
        float accel_peak_g = 0.0f;
    };

    ScrollTracker s_scroll = {};
    KarmaTracker s_karma = {};
    SysGestureProfile s_profile = SysGestureProfile::Default;
    uint32_t s_last_sequence = 0;
    uint32_t s_last_sample_us = 0;
    uint32_t s_cooldown_until_us = 0;
    uint32_t s_karma_cooldown_until_us = 0;
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
        s_karma = {};
    }

    void ClearEventQueue()
    {
        s_queue_head = 0;
        s_queue_count = 0;
        for (auto &event : s_queue)
            event = {};
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

    bool UpdateKarmaStrike(const SysMotionSample &sample)
    {
        const uint32_t now = sample.timestamp_us;
        const float abs_gx = fabsf(sample.imu.gxDps);
        const float abs_gy = fabsf(sample.imu.gyDps);
        const float abs_gz = fabsf(sample.imu.gzDps);
        const float cross_axis = fmaxf(abs_gx, abs_gy);
        const float accel_mag = sqrtf(sample.imu.axG * sample.imu.axG +
                                      sample.imu.ayG * sample.imu.ayG +
                                      sample.imu.azG * sample.imu.azG);

        if (DeadlinePending(now, s_karma_cooldown_until_us))
            return false;
        s_karma_cooldown_until_us = 0;

        if (!s_karma.active)
        {
            // 起始段必须由 gz 明显主导；旋钮滚动以 gy 为主，因此不会进入本状态机。
            if (abs_gz >= KARMA_ARM_DPS &&
                abs_gz >= cross_axis * KARMA_ARM_AXIS_DOMINANCE)
            {
                s_karma.active = true;
                s_karma.direction = sample.imu.gzDps >= 0.0f ? 1 : -1;
                s_karma.started_us = now;
                s_karma.primary_peak_dps = abs_gz;
                s_karma.return_peak_dps = 0.0f;
                s_karma.cross_axis_peak_dps = cross_axis;
                s_karma.accel_peak_g = accel_mag;
            }
            return false;
        }

        const uint32_t elapsed_us = now - s_karma.started_us;
        if (elapsed_us > KARMA_MAX_PHASE_US)
        {
            s_karma = {};
            return false;
        }

        /*
         * 若一个较弱的杂波先以错误方向启动，而真正主脉冲尚未达到确认阈值，则允许强反向段
         * 重新建档。这样不会因为敲击前的小回摆吞掉本次动作，同时已成形的主脉冲不会被改向。
         */
        if (sample.imu.gzDps * s_karma.direction < 0.0f &&
            s_karma.primary_peak_dps < KARMA_PRIMARY_PEAK_DPS &&
            abs_gz >= KARMA_ARM_DPS &&
            abs_gz >= cross_axis * KARMA_ARM_AXIS_DOMINANCE)
        {
            s_karma.direction = sample.imu.gzDps >= 0.0f ? 1 : -1;
            s_karma.started_us = now;
            s_karma.primary_peak_dps = abs_gz;
            s_karma.return_peak_dps = 0.0f;
            s_karma.cross_axis_peak_dps = cross_axis;
            s_karma.accel_peak_g = accel_mag;
            return false;
        }

        if (sample.imu.gzDps * s_karma.direction >= 0.0f)
            s_karma.primary_peak_dps = fmaxf(s_karma.primary_peak_dps, abs_gz);
        else
            s_karma.return_peak_dps = fmaxf(s_karma.return_peak_dps, abs_gz);
        s_karma.cross_axis_peak_dps = fmaxf(s_karma.cross_axis_peak_dps, cross_axis);
        s_karma.accel_peak_g = fmaxf(s_karma.accel_peak_g, accel_mag);

        if (elapsed_us < KARMA_MIN_PHASE_US ||
            s_karma.primary_peak_dps < KARMA_PRIMARY_PEAK_DPS ||
            s_karma.return_peak_dps < KARMA_RETURN_PEAK_DPS ||
            s_karma.primary_peak_dps <
                s_karma.cross_axis_peak_dps * KARMA_WHOLE_PHASE_DOMINANCE ||
            s_karma.accel_peak_g < KARMA_ACCEL_PEAK_G)
        {
            return false;
        }

        const int8_t direction = s_karma.direction;
        const float strength_dps = s_karma.primary_peak_dps;
        const SysGestureType type = direction > 0
                                        ? SysGestureType::KarmaStrikeA
                                        : SysGestureType::KarmaStrikeB;
        PushEvent(type, now, strength_dps, direction);

        // 同一物理波形只产生一个业力事件；滚动的半截状态也不能跨过本次高能敲击。
        ResetTracking();
        s_karma_cooldown_until_us = now + KARMA_COOLDOWN_US;
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

    if (s_profile == SysGestureProfile::Karma)
    {
        // 业力敲击与换武器共用 gz 主轴；专属上下文只运行业力状态机，避免同一波形抢先变成换武器。
        if (UpdateKarmaStrike(sample))
            return;
    }
    else
    {
        // 默认上下文保留原有换武器优先级，其他 App 不会执行业力敲击判定。
        if (DetectWeaponChange(sample))
            return;
    }

    UpdateScroll(sample);
}

void SysGesture_SetProfile(SysGestureProfile profile)
{
    if (profile == s_profile)
        return;

    s_profile = profile;
    ResetTracking();
    s_last_sample_us = 0;
    s_cooldown_until_us = 0;
    s_karma_cooldown_until_us = 0;
    ClearEventQueue();
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
    s_profile = SysGestureProfile::Default;
    s_last_sequence = 0;
    s_last_sample_us = 0;
    s_cooldown_until_us = 0;
    s_karma_cooldown_until_us = 0;
    ClearEventQueue();
}
