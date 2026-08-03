/*
【模块职责】人体坐标漂移调试页。入口按“屏幕朝上、底边朝向自己”自动校准，持续显示纯陀螺仪/
静止辅助姿态、人体坐标去重力加速度、漂移阈值时间和采样质量，供实机判断多久开始明显漂移。
【分层边界】本页只复制 SysMotion_GetLatest() 缓存中的 body_imu，不读取 BSP/I2C、不消费动作环，
也不改变正式双蛇杖识别器的阈值或业务状态。
【交互】旋钮切换姿态数值、加速度、统计和设备姿态四页；主键短按只暂停/恢复屏幕，后台积分
始终继续；侧键连续两次短按重新校准；
任一按键长按返回。超过 100ms 的采样断点会冻结为不连续状态，不能自动续接。
*/
#include "sys/app_base.h"

#include <stdio.h>

#include "lang/ui_strings.h"
#include "sys/app_manager.h"
#include "sys/sys_gesture.h"
#include "sys/sys_human_frame_tracker.h"
#include "sys/sys_motion.h"
#include "ui/ui_clock.h"
#include "ui/ui_pose_model.h"

namespace
{
    constexpr uint8_t PAGE_COUNT = 4;
    constexpr uint32_t DISPLAY_INTERVAL_MS = 200;
    constexpr uint32_t RECALIBRATION_CONFIRM_MS = 3000;
    constexpr uint32_t IDLE_KEEPALIVE_MS = 5000;

    void FormatElapsed(uint64_t elapsed_us, char *out, size_t out_size)
    {
        const uint64_t total_seconds = elapsed_us / 1000000ULL;
        const uint32_t hours = static_cast<uint32_t>(total_seconds / 3600ULL);
        const uint32_t minutes = static_cast<uint32_t>((total_seconds / 60ULL) % 60ULL);
        const uint32_t seconds = static_cast<uint32_t>(total_seconds % 60ULL);
        snprintf(out, out_size, "%02lu:%02lu:%02lu",
                 static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
    }

    void FormatThreshold(uint64_t elapsed_us, char *out, size_t out_size)
    {
        if (elapsed_us == 0)
        {
            snprintf(out, out_size, "--");
            return;
        }
        const double seconds = static_cast<double>(elapsed_us) / 1000000.0;
        if (seconds < 600.0)
            snprintf(out, out_size, "%.1fs", seconds);
        else
            snprintf(out, out_size, "%.1fm", seconds / 60.0);
    }
}

class AppHumanFrameDrift : public AppBase
{
private:
    SysHumanFrame::Tracker tracker_;
    SysHumanFrame::Snapshot displayed_snapshot_;
    uint32_t last_motion_sequence_ = 0;
    uint32_t last_draw_ms_ = 0;
    uint32_t last_idle_keepalive_ms_ = 0;
    uint32_t recalibration_armed_ms_ = 0;
    uint8_t page_ = 0;
    bool display_paused_ = false;
    bool recalibration_armed_ = false;

    /** 重新建立入口人体坐标并清空本轮统计；调用者负责随后立即重绘校准提示。 */
    void beginCalibration()
    {
        tracker_.Begin();
        displayed_snapshot_ = tracker_.GetSnapshot();
        last_motion_sequence_ = 0;
        recalibration_armed_ = false;
        recalibration_armed_ms_ = 0;
        display_paused_ = false;
        last_draw_ms_ = 0;
        last_idle_keepalive_ms_ = millis();
    }

    /**
     * 复制最新机身样本。sequence 只防止同一帧重复积分；若主循环跨过中间帧，追踪器会把丢序和
     * 时间间隔原样计入质量统计，而不是用固定 104Hz 伪造连续数据。
     */
    void updateTracker()
    {
        SysMotionSample motion = {};
        if (!SysMotion_GetLatest(&motion) || motion.sequence == last_motion_sequence_)
            return;
        last_motion_sequence_ = motion.sequence;

        SysHumanFrame::InputSample input = {};
        input.sequence = motion.sequence;
        input.timestamp_us = motion.timestamp_us;
        input.accel_fresh = motion.accel_fresh;
        input.gyro_fresh = motion.gyro_fresh;
        input.body_imu = motion.body_imu;
        tracker_.Update(input);
    }

    void drawCalibration(const SystemLang_t lang)
    {
        const char *prompt = UIStrings::CaduceusCalibrationPrompt(lang);
        const char *detail = UIStrings::CaduceusCalibrationDetail(lang);
        const int prompt_x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(prompt)) / 2);
        const int detail_x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(detail)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(prompt_x, 46, prompt, 0.0f, TFT_YELLOW);
        HAL_Screen_ShowChineseLine(detail_x, 72, detail);
        drawHint(UIStrings::HumanFrameCalibratingHint(lang), TFT_LIGHTGREY);
    }

    void drawDiscontinuous(const SystemLang_t lang)
    {
        const char *message = UIStrings::HumanFrameDiscontinuous(lang);
        const char *detail = UIStrings::HumanFrameDiscontinuousDetail(lang);
        const int message_x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(message)) / 2);
        const int detail_x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(detail)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(message_x, 46, message, 0.0f, TFT_RED);
        HAL_Screen_ShowChineseLine(detail_x, 72, detail);
        drawHint(UIStrings::HumanFrameRecalibrateHint(lang), TFT_LIGHTGREY);
    }

    void drawAttitudePage(const SysHumanFrame::Snapshot &snapshot, const SystemLang_t lang)
    {
        char elapsed[20];
        char line[96];
        FormatElapsed(snapshot.elapsed_us, elapsed, sizeof(elapsed));
        snprintf(line, sizeof(line), "%s  %s  %+7.3f deg/min",
                 UIStrings::HumanFrameAttitudePage(lang), elapsed,
                 snapshot.yaw_drift_rate_deg_per_min);
        HAL_Screen_ShowChineseLine(8, 28, line);

        snprintf(line, sizeof(line), "GYRO  Y%+7.2f  P%+7.2f  R%+7.2f",
                 snapshot.gyro_yaw_drift_deg,
                 snapshot.gyro_pitch_drift_deg,
                 snapshot.gyro_roll_drift_deg);
        HAL_Screen_ShowChineseLine(8, 50, line);

        snprintf(line, sizeof(line), "AID   Y%+7.2f  P%+7.2f  R%+7.2f",
                 snapshot.aided_yaw_drift_deg,
                 snapshot.aided_pitch_drift_deg,
                 snapshot.aided_roll_drift_deg);
        HAL_Screen_ShowChineseLine(8, 72, line);

        snprintf(line, sizeof(line), "BIAS  X%+6.3f  Y%+6.3f  Z%+6.3f dps",
                 snapshot.gyro_bias_dps.x,
                 snapshot.gyro_bias_dps.y,
                 snapshot.gyro_bias_dps.z);
        HAL_Screen_ShowChineseLine(8, 94, line);
    }

    void drawAccelerationPage(const SysHumanFrame::Snapshot &snapshot, const SystemLang_t lang)
    {
        char line[96];
        HAL_Screen_ShowChineseLine(8, 28, UIStrings::HumanFrameAccelerationPage(lang));
        snprintf(line, sizeof(line), "AID   X%+7.3f  Y%+7.3f  Z%+7.3f g",
                 snapshot.linear_accel_human_g.x,
                 snapshot.linear_accel_human_g.y,
                 snapshot.linear_accel_human_g.z);
        HAL_Screen_ShowChineseLine(8, 50, line);

        snprintf(line, sizeof(line), "GYRO  X%+7.3f  Y%+7.3f  Z%+7.3f g",
                 snapshot.gyro_only_linear_accel_human_g.x,
                 snapshot.gyro_only_linear_accel_human_g.y,
                 snapshot.gyro_only_linear_accel_human_g.z);
        HAL_Screen_ShowChineseLine(8, 72, line);

        snprintf(line, sizeof(line), "RES   NOW %.3f  RMS %.3f  MAX %.3f g",
                 snapshot.linear_residual_g,
                 snapshot.linear_rms_g,
                 snapshot.linear_max_g);
        HAL_Screen_ShowChineseLine(8, 94, line);
    }

    void drawStatisticsPage(const SysHumanFrame::Snapshot &snapshot, const SystemLang_t lang)
    {
        char yaw_1[16], yaw_3[16], yaw_5[16], yaw_10[16];
        char tilt_1[16], tilt_3[16], tilt_5[16];
        char line[112];
        FormatThreshold(snapshot.first_yaw_threshold_us[0], yaw_1, sizeof(yaw_1));
        FormatThreshold(snapshot.first_yaw_threshold_us[1], yaw_3, sizeof(yaw_3));
        FormatThreshold(snapshot.first_yaw_threshold_us[2], yaw_5, sizeof(yaw_5));
        FormatThreshold(snapshot.first_yaw_threshold_us[3], yaw_10, sizeof(yaw_10));
        FormatThreshold(snapshot.first_tilt_threshold_us[0], tilt_1, sizeof(tilt_1));
        FormatThreshold(snapshot.first_tilt_threshold_us[1], tilt_3, sizeof(tilt_3));
        FormatThreshold(snapshot.first_tilt_threshold_us[2], tilt_5, sizeof(tilt_5));

        snprintf(line, sizeof(line), "%s  YAW 1/3: %s / %s",
                 UIStrings::HumanFrameStatisticsPage(lang), yaw_1, yaw_3);
        HAL_Screen_ShowChineseLine(8, 28, line);
        snprintf(line, sizeof(line), "YAW 5/10: %s / %s", yaw_5, yaw_10);
        HAL_Screen_ShowChineseLine(8, 50, line);
        snprintf(line, sizeof(line), "TILT 1/3/5: %s / %s / %s", tilt_1, tilt_3, tilt_5);
        HAL_Screen_ShowChineseLine(8, 72, line);
        snprintf(line, sizeof(line), "N%lu M%lu G%lu A%lu D%lu",
                 static_cast<unsigned long>(snapshot.processed_samples),
                 static_cast<unsigned long>(snapshot.missed_sequences),
                 static_cast<unsigned long>(snapshot.gaps_over_30ms),
                 static_cast<unsigned long>(snapshot.stale_accel_samples),
                 static_cast<unsigned long>(snapshot.discontinuities));
        HAL_Screen_ShowChineseLine(8, 94, line);
    }

    /**
     * 设备姿态页不增加页眉、角度文本或坐标轴装饰，只保留测试名称和实体模型。BodyX对应横向
     * 几何长边，BodyY对应纵向短边，+BodyX/+BodyY为实物右上切角，青色内嵌面代表+BodyZ屏幕；
     * 模型使用静止辅助姿态，
     * 因此慢速倾斜可由重力纠正，航向仍会如实保留六轴漂移。
     */
    void drawDevicePosePage(const SysHumanFrame::Snapshot &snapshot)
    {
        UIPoseModel::DrawDevice(snapshot.aided_orientation,
                                HAL_Get_Screen_Width() / 2,
                                74,
                                28.0f);
    }

    /** 通用App页眉包含时间和横线；本调试页按用户要求只在左上角保留当前应用名称。 */
    void drawName(const SystemLang_t lang)
    {
        HAL_Screen_ShowChineseLine(8, 5, UIStrings::HumanFrameDriftTitle(lang));
    }

    /** 用一行纯文本保留必要操作状态，不绘制通用底栏、折角或其他装饰。 */
    void drawHint(const char *text, uint16_t color)
    {
        const int x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(text)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(x, 121, text, 0.0f, color);
    }

    void drawFrame()
    {
        const SystemLang_t lang = appManager.getLanguage();
        const SysHumanFrame::Snapshot live = tracker_.GetSnapshot();
        if (!display_paused_ || live.status != SysHumanFrame::Status::Tracking)
            displayed_snapshot_ = live;

        HAL_Sprite_Clear();
        drawName(lang);
        if (live.status == SysHumanFrame::Status::Calibrating)
        {
            drawCalibration(lang);
        }
        else if (live.status == SysHumanFrame::Status::Discontinuous)
        {
            drawDiscontinuous(lang);
        }
        else
        {
            if (page_ == 0)
                drawAttitudePage(displayed_snapshot_, lang);
            else if (page_ == 1)
                drawAccelerationPage(displayed_snapshot_, lang);
            else if (page_ == 2)
                drawStatisticsPage(displayed_snapshot_, lang);
            else
                drawDevicePosePage(displayed_snapshot_);

            if (recalibration_armed_)
                drawHint(UIStrings::HumanFrameRecalibrateConfirm(lang), TFT_YELLOW);
            else if (display_paused_)
                drawHint(UIStrings::HumanFramePausedHint(lang), TFT_YELLOW);
            else
                drawHint(UIStrings::HumanFrameRunningHint(lang), TFT_LIGHTGREY);
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        // Caduceus Profile 会关闭旧滚动/换武器语义；本页忽略动作事件，只使用物理旋钮切换统计页。
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        page_ = 0;
        beginCalibration();
        drawFrame();
    }

    void onResume() override
    {
        /*
         * 页面在后台期间不会消费最新缓存，恢复后无法知道缺失区间内是否转动，因此必须重新校准，
         * 不能从旧四元数继续积分并给出看似连续的漂移时间。
         */
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        beginCalibration();
        drawFrame();
    }

    void onBackground() override
    {
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onLoop() override
    {
        updateTracker();
        const uint32_t now = millis();
        /*
         * 这是用户主动进入的耐久诊断，可能连续放置数小时；定期刷新空闲计时，避免普通屏幕休眠
         * 中断 IMU 并把测试强制变成 Discontinuous。退出页面后不再刷新，全局待机策略立即恢复。
         */
        if (now - last_idle_keepalive_ms_ >= IDLE_KEEPALIVE_MS)
        {
            appManager.resetIdleTimer();
            last_idle_keepalive_ms_ = now;
        }
        if (recalibration_armed_ && now - recalibration_armed_ms_ > RECALIBRATION_CONFIRM_MS)
        {
            recalibration_armed_ = false;
            drawFrame();
            last_draw_ms_ = now;
        }
        else if (UIClock_Due(last_draw_ms_, DISPLAY_INTERVAL_MS))
        {
            drawFrame();
        }
    }

    void onDestroy() override
    {
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onKnob(int delta) override
    {
        if (delta == 0)
            return;
        int next = static_cast<int>(page_) + (delta > 0 ? 1 : -1);
        if (next < 0)
            next = PAGE_COUNT - 1;
        if (next >= PAGE_COUNT)
            next = 0;
        page_ = static_cast<uint8_t>(next);
        drawFrame();
    }

    void onKeyShort() override
    {
        if (tracker_.GetSnapshot().status != SysHumanFrame::Status::Tracking)
            return;
        display_paused_ = !display_paused_;
        if (!display_paused_)
            displayed_snapshot_ = tracker_.GetSnapshot();
        drawFrame();
    }

    void onBtn2Short() override
    {
        const uint32_t now = millis();
        if (recalibration_armed_ && now - recalibration_armed_ms_ <= RECALIBRATION_CONFIRM_MS)
        {
            beginCalibration();
            drawFrame();
            return;
        }
        recalibration_armed_ = true;
        recalibration_armed_ms_ = now;
        drawFrame();
    }

    void onBtn2Double() override
    {
        // HAL 已确认这是两次侧键短按，等价于完成页面内的二次确认，直接清空并重新建立入口坐标。
        beginCalibration();
        drawFrame();
    }

    void onKeyLong() override
    {
        appManager.popApp();
    }

    void onBtn2Long() override
    {
        onKeyLong();
    }
};

AppHumanFrameDrift instanceHumanFrameDrift;
AppBase *appHumanFrameDrift = &instanceHumanFrameDrift;
