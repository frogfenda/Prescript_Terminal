/*
【模块职责】双蛇杖专属“动作测试”页：启用专属识别上下文，并显示最近一次高置信度动作的
语义、横斩方向和角速度峰值，供 V4B 实机校准六分类；该页面不承担正式应用业务状态。
【调用关系】AppManager 通过统一 onGesture() 分发 SYS 事件；本页不读取 SysMotion、不复制阈值，
系统设置通过 AppId::CaduceusActionTest 进入，正式双蛇杖应用使用独立 App 类消费同一语义事件。
【阶段边界】当前不访问 FAT、不播放音频/震动、不判定武器对错，也不保存进度；长按返回上一级。
*/
#include "sys/app_base.h"

#include "lang/ui_strings.h"
#include "sys/app_manager.h"
#include "sys/sys_gesture.h"
#include "ui/ui_frame.h"

namespace
{
    /** 把稳定手势语义映射到 UIStrings 的动作名称下标；未知事件返回 -1 并由页面忽略。 */
    int CaduceusActionIndex(SysGestureType type)
    {
        switch (type)
        {
            case SysGestureType::HorizontalSlash: return 0;
            case SysGestureType::VerticalSlash: return 1;
            case SysGestureType::DiagonalSlashA: return 2;
            case SysGestureType::DiagonalSlashB: return 3;
            case SysGestureType::Thrust: return 4;
            case SysGestureType::Uppercut: return 5;
            default: return -1;
        }
    }
}

class AppCaduceusActionTest : public AppBase
{
private:
    bool has_event_ = false;
    bool calibrated_ = false;
    SysGestureEvent latest_event_ = {};

    void drawFrame()
    {
        const SystemLang_t lang = appManager.getLanguage();
        HAL_Sprite_Clear();
        drawAppWindow(UIStrings::CaduceusActionTestTitle(lang));

        if (!calibrated_)
        {
            const char *prompt = UIStrings::CaduceusCalibrationPrompt(lang);
            const char *detail = UIStrings::CaduceusCalibrationDetail(lang);
            const int prompt_x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(prompt)) / 2);
            const int detail_x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(detail)) / 2);
            HAL_Screen_ShowChineseLine_Faded_Color(prompt_x, 54, prompt, 0.0f, TFT_YELLOW);
            HAL_Screen_ShowChineseLine(detail_x, 80, detail);
        }
        else if (!has_event_)
        {
            const char *waiting = UIStrings::CaduceusWaiting(lang);
            const int x = max(6, (HAL_Get_Screen_Width() - HAL_Get_Text_Width(waiting)) / 2);
            HAL_Screen_ShowChineseLine_Faded_Color(x, 62, waiting, 0.0f, TFT_CYAN);
        }
        else
        {
            char line[80];
            const int action_index = CaduceusActionIndex(latest_event_.type);
            snprintf(line, sizeof(line), "%s%s",
                     UIStrings::CaduceusActionPrefix(lang),
                     UIStrings::CaduceusActionName(lang, action_index));
            HAL_Screen_ShowChineseLine_Faded_Color(12, 52, line, 0.0f, TFT_CYAN);

            const int horizontal_direction = latest_event_.type == SysGestureType::HorizontalSlash
                                                 ? latest_event_.direction
                                                 : 0;
            snprintf(line, sizeof(line), "%s%s",
                     UIStrings::CaduceusDirectionPrefix(lang),
                     UIStrings::CaduceusDirectionValue(lang, horizontal_direction));
            HAL_Screen_ShowChineseLine(12, 75, line);

            /*
             * 动作测试页同时显示共享核心的边界分数和触发到交付延迟，便于实机确认提前收窗
             * 是否真的改善手感。正式双蛇杖App仍只消费稳定动作语义，不依据这些诊断值二次分类。
             */
            snprintf(line, sizeof(line), "%s%.0f dps C%.0f%% L%lu",
                     UIStrings::CaduceusStrengthPrefix(lang), latest_event_.strength_dps,
                     latest_event_.confidence * 100.0f,
                     static_cast<unsigned long>(latest_event_.recognition_latency_us / 1000));
            HAL_Screen_ShowChineseLine(12, 98, line);
        }

        UIFrame::DrawTip(UIStrings::HoldBackHint(lang));
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        // Profile 切换会清空上一页残留事件和半截动作，页面只接收进入后重新完成的六种动作。
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        SysGesture_BeginCaduceusEntryCalibration();
        has_event_ = false;
        calibrated_ = false;
        latest_event_ = {};
        drawFrame();
    }

    void onResume() override
    {
        SysGesture_SetProfile(SysGestureProfile::Caduceus);
        // 从PushNotify等前台页面返回时Profile已被重置，必须重新启动放平校准，不能只画旧结果。
        SysGesture_BeginCaduceusEntryCalibration();
        calibrated_ = false;
        has_event_ = false;
        drawFrame();
    }

    void onBackground() override
    {
        // 专属识别不能在后台运行，否则斩击语义可能泄漏到后续页面。
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onLoop() override
    {
        const bool complete = SysGesture_IsCaduceusEntryCalibrationComplete();
        if (complete != calibrated_)
        {
            calibrated_ = complete;
            drawFrame();
        }
    }

    void onDestroy() override
    {
        SysGesture_SetProfile(SysGestureProfile::Default);
    }

    void onKnob(int delta) override
    {
        (void)delta;
    }

    void onGesture(const SysGestureEvent &event) override
    {
        if (!calibrated_)
            return;
        if (CaduceusActionIndex(event.type) < 0)
            return;
        latest_event_ = event;
        has_event_ = true;
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

AppCaduceusActionTest instanceCaduceusActionTest;
AppBase *appCaduceusActionTest = &instanceCaduceusActionTest;
