/*
【模块职责】统一人体运动服务的实机观察页。页面只消费SysHumanMotion只读快照，显示磁辅助人体姿态、
绝对人体坐标去重力加速度和带屏幕/切角的设备模型，不再私有积分第二套姿态或输出实验期高频日志。
【交互】进入页面时按“屏幕朝上、底边朝向自己”显式建立入口坐标；旋钮切换状态、加速度、立方体
三页；主键短按暂停/恢复显示（系统追踪继续）；侧键连续两次短按重新对齐；任一按键长按返回。
【解耦】页面不读取SysMotion、SysMag、BSP/I2C或校准文件，也不把地磁送入动作识别。地磁异常时
系统服务冻结最后航向修正，本页仍显示可退化的六轴连续姿态与明确质量状态。
*/
#include "sys/app_base.h"

#include <math.h>
#include <stdio.h>

#include "lang/ui_strings.h"
#include "sys/app_manager.h"
#include "sys/sys_gesture.h"
#include "sys/sys_human_motion.h"
#include "ui/ui_clock.h"
#include "ui/ui_pose_model.h"

namespace
{
    constexpr uint8_t PAGE_COUNT = 3;
    constexpr uint32_t DISPLAY_INTERVAL_MS = 200;
    constexpr uint32_t RECALIBRATION_CONFIRM_MS = 3000;
    constexpr uint32_t IDLE_KEEPALIVE_MS = 5000;
    constexpr float STANDARD_GRAVITY_MPS2 = 9.80665f;

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
}

class AppHumanFrameDrift : public AppBase
{
private:
    SysHumanMotion::Snapshot displayed_snapshot_;
    uint32_t last_draw_ms_ = 0;
    uint32_t last_idle_keepalive_ms_ = 0;
    uint32_t recalibration_armed_ms_ = 0;
    uint8_t page_ = 0;
    bool display_paused_ = false;
    bool recalibration_armed_ = false;

    /**
     * 入口对齐属于系统服务的显式生命周期，不属于页面私有算法。调用后清空上一轮人体方向、磁参考和
     * 纠偏量；页面仅重置自己的显示/二次确认状态，下一轮样本由主循环统一推进。
     */
    void beginAlignment()
    {
        SysHumanMotion::BeginAlignment();
        displayed_snapshot_ = {};
        SysHumanMotion::GetSnapshot(&displayed_snapshot_);
        recalibration_armed_ = false;
        recalibration_armed_ms_ = 0;
        display_paused_ = false;
        last_draw_ms_ = 0;
        last_idle_keepalive_ms_ = millis();
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

    /**
     * 正式状态页只保留对判断输出是否可用有意义的数据：磁辅助相对入口姿态、累计航向修正、当前
     * 退化/纠偏状态与地磁质量。实验期A/R时间对齐、纯陀螺阈值统计和高频串口对照已删除。
     */
    void drawOrientationPage(const SysHumanMotion::Snapshot &snapshot)
    {
        const SysHumanFrame::Snapshot &base = snapshot.base;
        const SysMagHeading::Snapshot &heading = snapshot.magnetic_heading;
        const SysMagAidedOrientation::Snapshot &aided = snapshot.magnetic_orientation;
        char elapsed[20];
        char line[112];
        FormatElapsed(base.elapsed_us, elapsed, sizeof(elapsed));

        snprintf(line, sizeof(line), "ABS  Y%+7.2f  P%+7.2f  R%+7.2f",
                 base.aided_yaw_drift_deg + aided.correction_deg,
                 base.aided_pitch_drift_deg,
                 base.aided_roll_drift_deg);
        HAL_Screen_ShowChineseLine(8, 28, line);

        snprintf(line, sizeof(line), "CORR %+7.2f  REM %+7.2f  %s",
                 aided.correction_deg,
                 aided.remaining_correction_deg,
                 SysMagAidedOrientation::Tracker::StateName(aided.state));
        HAL_Screen_ShowChineseLine(8, 50, line);

        snprintf(line, sizeof(line), "MAG  F%.1f H%.1f C%.2f  %s%s",
                 heading.field_strength_uT,
                 heading.horizontal_field_uT,
                 heading.confidence,
                 heading.accepted ? "OK" : SysMagHeading::Constraint::RejectReasonName(
                                                   heading.reject_reason),
                 heading.sync_pending ? "/SYNC" : "");
        HAL_Screen_ShowChineseLine(8, 72, line);

        snprintf(line, sizeof(line), "TIME %s  G%.1fdps A%.2fg",
                 elapsed,
                 aided.gyro_magnitude_dps,
                 aided.accel_delta_g);
        HAL_Screen_ShowChineseLine(8, 94, line);
    }

    /**
     * 同时给出g和m/s²，便于后续动作算法直接选择物理单位。这里是磁辅助Human X/Y/Z中的线性
     * 加速度；“绝对”表示入口水平航向由地磁长期稳定，不表示对速度或位置做惯导积分。
     */
    void drawAccelerationPage(const SysHumanMotion::Snapshot &snapshot,
                              const SystemLang_t lang)
    {
        const SysHumanFrame::Vector3 &accel = snapshot.absolute_linear_accel_human_g;
        const float magnitude = sqrtf(accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);
        char line[112];
        HAL_Screen_ShowChineseLine(8, 28, UIStrings::HumanFrameAccelerationPage(lang));

        snprintf(line, sizeof(line), "g     X%+7.3f  Y%+7.3f  Z%+7.3f",
                 accel.x, accel.y, accel.z);
        HAL_Screen_ShowChineseLine(8, 50, line);

        snprintf(line, sizeof(line), "m/s2  X%+7.2f  Y%+7.2f  Z%+7.2f",
                 accel.x * STANDARD_GRAVITY_MPS2,
                 accel.y * STANDARD_GRAVITY_MPS2,
                 accel.z * STANDARD_GRAVITY_MPS2);
        HAL_Screen_ShowChineseLine(8, 72, line);

        snprintf(line, sizeof(line), "NORM %.3fg  %s%s",
                 magnitude,
                 snapshot.absolute_linear_accel_valid ? "VALID" : "INVALID",
                 snapshot.absolute_linear_accel_fresh ? "/FRESH" : "/HELD");
        HAL_Screen_ShowChineseLine(8, 94, line);
    }

    /**
     * 设备模型沿用实板确认的长短边、内嵌屏幕与右上切角几何，只输入系统发布的磁辅助四元数。
     * 页面不再保留六轴基础立方体，避免把已完成使命的对照视图误当正式输出。
     */
    void drawDevicePosePage(const SysHumanMotion::Snapshot &snapshot)
    {
        if (!snapshot.magnetic_orientation.orientation_valid)
            return;
        UIPoseModel::DrawDevice(snapshot.magnetic_orientation.orientation,
                                HAL_Get_Screen_Width() / 2,
                                74,
                                28.0f);
    }

    /** 用户此前要求不使用通用页眉装饰，因此这里只在左上角保留当前应用名称。 */
    void drawName(const SystemLang_t lang)
    {
        HAL_Screen_ShowChineseLine(8, 5, UIStrings::HumanFrameDriftTitle(lang));
    }

    void drawHint(const char *text, uint16_t color)
    {
        const int x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(text)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(x, 121, text, 0.0f, color);
    }

    void drawFrame()
    {
        const SystemLang_t lang = appManager.getLanguage();
        SysHumanMotion::Snapshot live = {};
        if (!SysHumanMotion::GetSnapshot(&live))
            live = {};
        if (!display_paused_ || live.base.status != SysHumanFrame::Status::Tracking)
            displayed_snapshot_ = live;

        HAL_Sprite_Clear();
        drawName(lang);
        if (!live.alignment_active || live.base.status == SysHumanFrame::Status::Calibrating)
        {
            drawCalibration(lang);
        }
        else if (live.base.status == SysHumanFrame::Status::Discontinuous)
        {
            drawDiscontinuous(lang);
        }
        else
        {
            if (page_ == 0)
                drawOrientationPage(displayed_snapshot_);
            else if (page_ == 1)
                drawAccelerationPage(displayed_snapshot_, lang);
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
        // Caduceus Profile关闭旧滚动动作，防止挥动设备时误切页面；本页仍不消费任何动作语义。
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        page_ = 0;
        beginAlignment();
        drawFrame();
    }

    void onResume() override
    {
        /* 系统服务在页面后台仍持续消费缓存，因此返回本页可安全延续；若期间睡眠造成真实断点，
         * Tracker会明确进入Discontinuous，页面要求用户重新对齐而不会伪装连续。 */
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        last_idle_keepalive_ms_ = millis();
        drawFrame();
    }

    void onBackground() override
    {
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onLoop() override
    {
        const uint32_t now = millis();
        /* 用户可能把该页作为数小时耐久测试；只在页面前台续空闲计时，退出后恢复全局待机策略。 */
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
        SysHumanMotion::Snapshot live = {};
        if (!SysHumanMotion::GetSnapshot(&live) ||
            live.base.status != SysHumanFrame::Status::Tracking)
        {
            return;
        }
        display_paused_ = !display_paused_;
        if (!display_paused_)
            displayed_snapshot_ = live;
        drawFrame();
    }

    void onBtn2Short() override
    {
        const uint32_t now = millis();
        if (recalibration_armed_ && now - recalibration_armed_ms_ <= RECALIBRATION_CONFIRM_MS)
        {
            beginAlignment();
            drawFrame();
            return;
        }
        recalibration_armed_ = true;
        recalibration_armed_ms_ = now;
        drawFrame();
    }

    void onBtn2Double() override
    {
        // HAL已确认两次侧键短按，等价于完成页面内二次确认，直接清空并重新建立入口坐标。
        beginAlignment();
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
