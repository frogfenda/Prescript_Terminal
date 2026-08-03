/*
【模块职责】独立地磁诊断与校准页。显示SysMag缓存中的原始/校准磁场、场强、置信度和轴映射状态，
并驱动三维椭球校准会话。
【分层边界】本页不读取BSP/Wire1、不自行保存文件、不修改姿态或动作识别；采样、拟合和持久化
全部由SysMag拥有。
【交互】旋钮切换实时/校准页；校准页主键开始或结束拟合，侧键取消；长按返回。
*/
#include "sys/app_base.h"

#include <stdio.h>

#include "lang/ui_strings.h"
#include "sys/app_manager.h"
#include "sys/sys_mag.h"
#include "ui/ui_clock.h"

namespace
{
    static constexpr uint32_t DISPLAY_INTERVAL_MS = 200;
    static constexpr uint32_t IDLE_KEEPALIVE_MS = 5000;
    static constexpr uint8_t PAGE_COUNT = 2;
}

class AppMagDiagnostics : public AppBase
{
private:
    uint8_t page_ = 0;
    uint32_t last_draw_ms_ = 0;
    uint32_t last_idle_keepalive_ms_ = 0;
    SysMagCalibration::Result last_fit_;
    bool has_fit_result_ = false;

    void drawName(SystemLang_t lang)
    {
        HAL_Screen_ShowChineseLine(8, 5, UIStrings::MagDiagnosticsTitle(lang));
    }

    void drawHint(const char *text, uint16_t color = TFT_LIGHTGREY)
    {
        const int x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(text)) / 2);
        HAL_Screen_ShowChineseLine_Faded_Color(x, 121, text, 0.0f, color);
    }

    void drawLive(SystemLang_t lang)
    {
        SysMagSample sample = {};
        if (!SysMag_GetLatest(&sample))
        {
            SysMagServiceStatus status = {};
            SysMag_GetStatus(&status);
            char line[112];
            snprintf(line, sizeof(line), "STATE %s  ERR %s(%u)",
                     status.sleeping ? "SLEEP" : (status.available ? "WAIT_DATA" : "OFFLINE"),
                     BSP::Qmc5883::ErrorName(status.last_error),
                     static_cast<unsigned>(status.last_error));
            HAL_Screen_ShowChineseLine_Faded_Color(8, 28, line, 0.0f, TFT_RED);
            snprintf(line, sizeof(line), "ADDR %02X ACK:%c  CHIP_ID:%s%02X",
                     status.sensor.requestedAddress,
                     status.sensor.addressAcknowledged ? 'Y' : 'N',
                     status.sensor.chipIdValid ? "" : "--/",
                     status.sensor.chipId);
            HAL_Screen_ShowChineseLine(8, 50, line);
            snprintf(line, sizeof(line), "REG29:%s%02X C1:%s%02X C2:%s%02X",
                     status.sensor.axisSignValid ? "" : "--/", status.sensor.axisSign,
                     status.sensor.ctrl1Valid ? "" : "--/", status.sensor.ctrl1,
                     status.sensor.ctrl2Valid ? "" : "--/", status.sensor.ctrl2);
            HAL_Screen_ShowChineseLine(8, 72, line);
            snprintf(line, sizeof(line), "STATUS:%s%02X  %s",
                     status.sensor.statusValid ? "" : "--/", status.sensor.status,
                     status.available ? "等待第一帧" : UIStrings::MagUnavailable(lang));
            HAL_Screen_ShowChineseLine_Faded_Color(8, 94, line, 0.0f, TFT_RED);
            drawHint(UIStrings::MagLiveHint(lang));
            return;
        }

        char line[112];
        snprintf(line, sizeof(line), "%s 0x%02X AXIS:%s RNG:%s CAL:%s",
                 BSP::Qmc5883::TypeName(sample.sensor_type), sample.address,
                 sample.axis_mapping_verified ? "OK" : "?",
                 sample.range_configuration_verified ? "OK" : "?",
                 sample.calibrated ? "OK" : "NO");
        HAL_Screen_ShowChineseLine(8, 28, line);
        snprintf(line, sizeof(line), "RAW   X%6d  Y%6d  Z%6d",
                 sample.raw_x, sample.raw_y, sample.raw_z);
        HAL_Screen_ShowChineseLine(8, 50, line);
        snprintf(line, sizeof(line), "SENS  X%+7.2f Y%+7.2f Z%+7.2f uT",
                 sample.sensor_uT.x, sample.sensor_uT.y, sample.sensor_uT.z);
        HAL_Screen_ShowChineseLine(8, 72, line);
        snprintf(line, sizeof(line), "FIELD %6.2fuT CONF %.2f FLAGS 0x%02lX",
                 sample.field_strength_uT, sample.confidence,
                 static_cast<unsigned long>(sample.disturbance_reasons));
        HAL_Screen_ShowChineseLine(8, 94, line);
        drawHint(UIStrings::MagLiveHint(lang));
    }

    void drawCalibration(SystemLang_t lang)
    {
        SysMagCalibrationProgress progress = {};
        SysMag_GetCalibrationProgress(&progress);
        SysMagCalibration::Result saved = {};
        const bool has_saved = SysMag_GetCalibration(&saved);
        char line[112];

        if (progress.active)
        {
            snprintf(line, sizeof(line), "COLLECT  %lu / %lu",
                     static_cast<unsigned long>(progress.sample_count),
                     static_cast<unsigned long>(progress.capacity));
            HAL_Screen_ShowChineseLine_Faded_Color(8, 28, line, 0.0f, TFT_YELLOW);
            snprintf(line, sizeof(line), "SPAN  X%.1f  Y%.1f  Z%.1f uT",
                     progress.span_uT.x, progress.span_uT.y, progress.span_uT.z);
            HAL_Screen_ShowChineseLine(8, 50, line);
            HAL_Screen_ShowChineseLine(8, 72, UIStrings::MagCalibrationRunningHint(lang));
            HAL_Screen_ShowChineseLine(8, 94, UIStrings::MagCalibrationCancelHint(lang));
            drawHint(UIStrings::MagCalibrationRunningHint(lang), TFT_YELLOW);
            return;
        }

        snprintf(line, sizeof(line), "SAVED  %s  SAMPLES %lu",
                 has_saved ? "YES" : "NO",
                 static_cast<unsigned long>(has_saved ? saved.sample_count : 0U));
        HAL_Screen_ShowChineseLine(8, 28, line);
        if (has_saved)
        {
            snprintf(line, sizeof(line), "BIAS  X%+.1f Y%+.1f Z%+.1f uT",
                     saved.bias_uT.x, saved.bias_uT.y, saved.bias_uT.z);
            HAL_Screen_ShowChineseLine(8, 50, line);
            snprintf(line, sizeof(line), "FIELD %.2fuT  COVER %u/26  RMS %.2f%%",
                     saved.reference_field_uT, saved.coverage_bins,
                     saved.residual_rms_ratio * 100.0f);
            HAL_Screen_ShowChineseLine(8, 72, line);
        }
        else
        {
            HAL_Screen_ShowChineseLine(8, 50, "BIAS  --");
            HAL_Screen_ShowChineseLine(8, 72, "FIELD --  COVER --  RMS --");
        }

        if (has_fit_result_ && !last_fit_.valid)
        {
            snprintf(line, sizeof(line), "LAST  %s  SPAN %.0f/%.0f/%.0f",
                     SysMagCalibration::FailureName(last_fit_.failure),
                     last_fit_.span_uT.x, last_fit_.span_uT.y, last_fit_.span_uT.z);
            HAL_Screen_ShowChineseLine_Faded_Color(8, 94, line, 0.0f, TFT_RED);
        }
        else
        {
            HAL_Screen_ShowChineseLine(8, 94, "3D ELLIPSOID HARD/SOFT IRON");
        }
        drawHint(UIStrings::MagCalibrationReadyHint(lang));
    }

    void drawFrame()
    {
        const SystemLang_t lang = appManager.getLanguage();
        HAL_Sprite_Clear();
        drawName(lang);
        if (page_ == 0)
            drawLive(lang);
        else
            drawCalibration(lang);
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        page_ = 0;
        has_fit_result_ = false;
        last_draw_ms_ = 0;
        last_idle_keepalive_ms_ = millis();
        SysMag_SetDiagnosticLogging(true);
        drawFrame();
    }

    void onResume() override
    {
        SysMag_SetDiagnosticLogging(true);
        last_idle_keepalive_ms_ = millis();
        drawFrame();
    }

    void onBackground() override
    {
        SysMag_SetDiagnosticLogging(false);
        SysMag_CancelCalibration();
    }

    void onDestroy() override
    {
        SysMag_SetDiagnosticLogging(false);
        SysMag_CancelCalibration();
    }

    void onLoop() override
    {
        const uint32_t now = millis();
        if (now - last_idle_keepalive_ms_ >= IDLE_KEEPALIVE_MS)
        {
            appManager.resetIdleTimer();
            last_idle_keepalive_ms_ = now;
        }
        if (UIClock_Due(last_draw_ms_, DISPLAY_INTERVAL_MS))
            drawFrame();
    }

    void onKnob(int delta) override
    {
        if (delta == 0)
            return;
        page_ = static_cast<uint8_t>((page_ + 1) % PAGE_COUNT);
        drawFrame();
    }

    void onKeyShort() override
    {
        if (page_ == 0)
        {
            page_ = 1;
            drawFrame();
            return;
        }
        SysMagCalibrationProgress progress = {};
        SysMag_GetCalibrationProgress(&progress);
        if (!progress.active)
        {
            has_fit_result_ = false;
            SysMag_StartCalibration();
        }
        else
        {
            has_fit_result_ = true;
            SysMag_FinishCalibration(&last_fit_);
        }
        drawFrame();
    }

    void onBtn2Short() override
    {
        SysMagCalibrationProgress progress = {};
        SysMag_GetCalibrationProgress(&progress);
        if (progress.active)
        {
            SysMag_CancelCalibration();
            drawFrame();
        }
    }

    void onKeyLong() override { appManager.popApp(); }
    void onBtn2Long() override { onKeyLong(); }
};

AppMagDiagnostics instanceMagDiagnostics;
AppBase *appMagDiagnostics = &instanceMagDiagnostics;
