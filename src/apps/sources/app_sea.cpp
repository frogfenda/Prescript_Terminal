/*
【模块职责】沉浸式“海”应用。读取 SysMotion 的共享六轴缓存，通过现有 MahonySolver 解算左右倾角，
再统一生成倾角、角速度、角加速度和去重力线性加速度，交给 UIFluidSurface 驱动多层海面。
【分层边界】本 App 不访问 BSP/I2C，不复制手势识别逻辑；流体数值与底层绘制归 UIFluidSurface，
页面只管理传感器到视觉输入、生命周期、空闲计时和未来字幕最高层。
【交互约定】长按返回；旋钮和离散滚动手势在页面内不改变海面。明显的真实转动会刷新空闲计时，
设备静止后仍遵守全局待机设置。
*/
#include "sys/app_base.h"

#include <math.h>

#include "hal/hal.h"
#include "sys/app_manager.h"
#include "sys/sys_feedback.h"
#include "sys/sys_motion.h"
#include "sys/sys_pose_solver.h"
#include "ui/ui_clock.h"
#include "ui/ui_fluid_surface.h"
#include "ui/ui_theme.h"

namespace
{
    constexpr float IMU_SAMPLE_HZ = 104.0f;
    constexpr uint32_t MOTION_STALE_MS = 160;
    constexpr uint32_t MOTION_DISCONTINUITY_US = 100000;
    constexpr float IDLE_ROLL_DELTA_DEG = 0.8f;
    constexpr float IDLE_ROLL_RATE_DPS = 14.0f;
    constexpr float IDLE_LINEAR_ACCEL_G = 0.075f;
    constexpr float LINEAR_ACCEL_DEAD_ZONE_G = 0.025f;
    constexpr float ANGULAR_ACCEL_FILTER_HZ = 18.0f;
    constexpr uint32_t IDLE_REFRESH_INTERVAL_MS = 350;

    float ClampFloat(float value, float low, float high)
    {
        if (value < low)
            return low;
        if (value > high)
            return high;
        return value;
    }

    /** 去除静止传感器噪声，同时保留超过阈值后的连续幅值，避免响应在阈值处突然跳变。 */
    float ApplyDeadZone(float value, float dead_zone)
    {
        if (fabsf(value) <= dead_zone)
            return 0.0f;
        return value > 0.0f ? value - dead_zone : value + dead_zone;
    }
}

class AppSea : public AppBase
{
private:
    UIFluidSurface fluid_;
    SysPose::MahonySolver pose_solver_;
    uint32_t last_motion_sequence_ = 0;
    uint32_t last_motion_timestamp_us_ = 0;
    uint32_t last_motion_received_ms_ = 0;
    uint32_t last_physics_ms_ = 0;
    uint32_t last_render_ms_ = 0;
    uint32_t last_idle_refresh_ms_ = 0;
    UIFluidInput fluid_input_ = {};
    float previous_roll_rate_dps_ = 0.0f;
    float filtered_roll_accel_dps2_ = 0.0f;
    float previous_activity_roll_deg_ = 0.0f;
    bool pose_valid_ = false;

    /**
     * 消费 SysMotion 最新样本并更新姿态；sequence 保证一个样本只进入 Mahony 一次。
     * 采样中断超过 100ms 时重置解算器，避免 Light Sleep、I2C 恢复或后台停顿后的旧积分污染姿态。
     */
    void updateMotionInput()
    {
        SysMotionSample sample = {};
        if (!SysMotion_GetLatest(&sample) || sample.sequence == last_motion_sequence_)
            return;

        last_motion_sequence_ = sample.sequence;
        if (!sample.gyro_fresh)
            return;

        const uint32_t previous_timestamp_us = last_motion_timestamp_us_;
        const bool discontinuity = previous_timestamp_us != 0 &&
                                   sample.timestamp_us - previous_timestamp_us > MOTION_DISCONTINUITY_US;
        if (discontinuity)
        {
            pose_solver_.Begin(IMU_SAMPLE_HZ);
            pose_valid_ = false;
            filtered_roll_accel_dps2_ = 0.0f;
        }
        last_motion_timestamp_us_ = sample.timestamp_us;

        float sample_dt_seconds = 1.0f / IMU_SAMPLE_HZ;
        if (previous_timestamp_us != 0 && !discontinuity)
        {
            sample_dt_seconds = ClampFloat((float)(sample.timestamp_us - previous_timestamp_us) / 1000000.0f,
                                           0.002f,
                                           0.050f);
        }

        pose_solver_.Update(sample.imu);
        const SysPose::Result pose = pose_solver_.GetResult(false);
        if (!pose.valid)
            return;

        fluid_input_.roll_deg = pose.euler.rollDeg;
        fluid_input_.roll_rate_dps = sample.imu.gxDps;

        /*
         * 角加速度使用相邻真实 IMU 时间戳求导，再做一次快速低通。它只用于“停止转动后的反向回摆”，
         * 不参与姿态解算；采样中断后的第一帧清零，避免把长时间间隔误判成一次巨大冲击。
         */
        const float raw_roll_accel = (previous_timestamp_us == 0 || discontinuity)
                                         ? 0.0f
                                         : (sample.imu.gxDps - previous_roll_rate_dps_) / sample_dt_seconds;
        const float accel_alpha = ClampFloat(sample_dt_seconds * ANGULAR_ACCEL_FILTER_HZ, 0.0f, 1.0f);
        filtered_roll_accel_dps2_ += (raw_roll_accel - filtered_roll_accel_dps2_) * accel_alpha;
        fluid_input_.roll_accel_dps2 = filtered_roll_accel_dps2_;
        previous_roll_rate_dps_ = sample.imu.gxDps;

        if (sample.accel_fresh)
        {
            /*
             * Mahony 四元数已经描述机身姿态，可直接计算机身 Y/Z 轴应看到的单位重力分量。
             * 实测加速度减去该分量后得到线性运动，避免“只是倾斜设备”被重复当成横向冲击。
             */
            const SysPose::Quaternion &q = pose.quaternion;
            const float expected_gravity_y = 2.0f * (q.w * q.x + q.y * q.z);
            const float expected_gravity_z = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
            fluid_input_.lateral_accel_g = ApplyDeadZone(sample.imu.ayG - expected_gravity_y,
                                                         LINEAR_ACCEL_DEAD_ZONE_G);
            fluid_input_.vertical_accel_g = ApplyDeadZone(sample.imu.azG - expected_gravity_z,
                                                          LINEAR_ACCEL_DEAD_ZONE_G);
        }

        fluid_input_.valid = true;
        pose_valid_ = true;
        last_motion_received_ms_ = millis();

        /*
         * 连续姿态不是 AppManager 的实体按键/旋钮事件，必须由页面主动声明“用户仍在交互”。
         * 只对角度变化或真实转速响应，并限频刷新，避免静止噪声让设备永远无法自动待机。
         */
        const float roll_delta = fabsf(fluid_input_.roll_deg - previous_activity_roll_deg_);
        const float linear_activity = fabsf(fluid_input_.lateral_accel_g) +
                                      fabsf(fluid_input_.vertical_accel_g);
        const uint32_t now = millis();
        if ((roll_delta >= IDLE_ROLL_DELTA_DEG ||
             fabsf(fluid_input_.roll_rate_dps) >= IDLE_ROLL_RATE_DPS ||
             linear_activity >= IDLE_LINEAR_ACCEL_G) &&
            now - last_idle_refresh_ms_ >= IDLE_REFRESH_INTERVAL_MS)
        {
            appManager.resetIdleTimer();
            last_idle_refresh_ms_ = now;
            previous_activity_roll_deg_ = fluid_input_.roll_deg;
        }
    }

    /**
     * 最高前景层预留点。
     * 当前不猜测字幕来源、所有权和生命周期；后续确定字幕接口后，只在这里使用 UIText 绘制，
     * 即可保证文字位于天空、远浪、主水体、泡沫和水下亮点之上且不参与流体变形。
     */
    void drawForegroundOverlay()
    {
        // 字幕需求确认前保持空实现，避免提前引入不可靠的 String/指针生命周期接口。
    }

    /** 清空并按固定图层顺序绘制一帧，整帧最后只推屏一次。 */
    void drawFrame()
    {
        HAL_Sprite_Clear();
        fluid_.draw();
        drawForegroundOverlay();
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        pose_solver_.Begin(IMU_SAMPLE_HZ);
        fluid_.reset(HAL_Get_Screen_Width(), HAL_Get_Screen_Height());
        last_motion_sequence_ = 0;
        last_motion_timestamp_us_ = 0;
        last_motion_received_ms_ = 0;
        last_physics_ms_ = millis();
        last_render_ms_ = 0;
        last_idle_refresh_ms_ = 0;
        fluid_input_ = {};
        previous_roll_rate_dps_ = 0.0f;
        filtered_roll_accel_dps2_ = 0.0f;
        previous_activity_roll_deg_ = 0.0f;
        pose_valid_ = false;
    }

    void onResume() override
    {
        // 返回前台时从当前时间重新累计，不补算后台期间的流体步骤。
        last_physics_ms_ = millis();
        last_render_ms_ = 0;
        last_motion_sequence_ = 0;
        drawFrame();
    }

    void onBackground() override
    {
        // 页面不可见时停止注入最后一次动态输入；SysMotion 仍由系统主循环统一维护。
        fluid_input_.roll_rate_dps = 0.0f;
        fluid_input_.roll_accel_dps2 = 0.0f;
        fluid_input_.lateral_accel_g = 0.0f;
        fluid_input_.vertical_accel_g = 0.0f;
        fluid_input_.valid = false;
    }

    void onLoop() override
    {
        updateMotionInput();

        const uint32_t now = millis();
        if (now - last_physics_ms_ >= UITheme::FRAME_FAST_MS)
        {
            const float dt_seconds = (float)(now - last_physics_ms_) / 1000.0f;
            last_physics_ms_ = now;
            UIFluidInput frame_input = fluid_input_;
            frame_input.valid = pose_valid_ &&
                                now - last_motion_received_ms_ <= MOTION_STALE_MS;
            fluid_.update(dt_seconds, frame_input);
        }

        // 物理以约 60Hz 推进，整屏旋转/QSPI 刷新锁定约 30FPS，兼顾波动连续性和 TE 等待成本。
        if (UIClock_Due(last_render_ms_, UITheme::FRAME_NORMAL_MS))
            drawFrame();
    }

    void onDestroy() override {}

    // Sea 页面不把实体旋钮或全局摇动滚动映射为业务操作，避免倾斜观看时误改状态。
    void onKnob(int delta) override { (void)delta; }

    void onKeyLong() override
    {
        Feedback_PlayBack();
        appManager.popApp();
    }
};

AppSea instanceSea;
AppBase *appSea = &instanceSea;
