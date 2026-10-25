/*
【模块职责】TT2 的应用业务及设置/运行页面；后台 tick 推进计时，进出页面不清除运行状态。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_countdown.cpp
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"
#include "ui/ui_frame.h"

// 【计时业务开始】
/* 【所有权】持续时长只由共用内核推进；完成提示未入队时保留事实，进出页面不能清掉它。 */
#include "apps/app_countdown.h"
#include "sys/sys_clock_rules.h"
#include "sys/sys_config.h"
#include "sys/sys_reminder.h"
#include "sys/sys_sleep_scheduler.h"
#include "sys/sys_feedback.h"
#include "lang/ui_strings.h"
namespace {
SysDurationTimer timer;
String completion_text;
int configured_minutes=0,configured_seconds=0;
uint32_t session=0;
}
bool Countdown_TryStart(int minutes,int seconds,const char *text)
{
    if (!SysClockRules::CountdownDuration(minutes,seconds)) return false;
    const uint32_t now=millis();
    SysReminder_CancelClock(SysClockReminderSource::Countdown);
    timer.start(((uint32_t)minutes*60U+seconds)*1000U,now);
    configured_minutes=minutes; configured_seconds=seconds; completion_text=text ? text : "";
    if (++session==0) ++session;
    SysSleep_ScheduleDeadlineMs(SysSleepSource::Countdown,timer.snapshot(now).deadline_ms,SysSleepWakeAction::Foreground);
    return true;
}
void Countdown_Update(uint32_t now)
{
    if (timer.advance(now)) {
        SysSleep_Cancel(SysSleepSource::Countdown);
        // 完成正文只生成一次；队列满时每轮仅重试投递，不反复分配/格式化长字符串。
        if (completion_text.isEmpty()) {
            char buffer[128];
            snprintf(buffer,sizeof(buffer),UIStrings::CountdownDoneFormat(TerminalLang::Normalize(sysConfig.language)),
                     configured_minutes,configured_seconds);
            completion_text=buffer;
        }
    }
    if (!timer.snapshot(now).completion_pending) return;
    if (SysReminder_SubmitClock(SysClockReminderSource::Countdown,session,SysReminderPresentation::Interrupt,
                                SysReminderKind::Custom,completion_text.c_str())) {
        timer.acknowledge(); Feedback_PlayTimerDone();
    }
}
bool Countdown_Cancel()
{
    const uint32_t now=millis(); Countdown_Update(now);
    if (!timer.cancel(now)) return false; // 已经到期的事实不能被取消按钮擦掉。
    SysSleep_Cancel(SysSleepSource::Countdown); SysReminder_CancelClock(SysClockReminderSource::Countdown);
    return true;
}
CountdownSnapshot Countdown_GetSnapshot()
{
    const auto state=timer.snapshot(millis());
    return {state,state.state==SysDurationState::Running || state.state==SysDurationState::Paused || state.completion_pending,
            configured_minutes,configured_seconds};
}
void Countdown_Start(int minutes,int seconds,const char *text)
{ if (!Countdown_TryStart(minutes,seconds,text)) Serial.println("[倒计时] 时长超出允许范围，本次启动未执行。"); }
bool Countdown_IsActive() { return Countdown_GetSnapshot().active; }
int Countdown_GetRemainingSeconds() { return (int)SysDurationDisplaySeconds(Countdown_GetSnapshot().timer.remaining_ms); }
// 【计时业务结束】

class AppCountdown : public AppBase {
private:
    int m, s, phase;

    DialAnimator dialAnim;       // 机械滚轮引擎
    TacticalLinkEngine linkAnim; // 战术链路引擎

    int last_sec = -1;

    // 【函数说明】绘制 TT2 页面：设置阶段显示分钟/秒钟滚轮，运行阶段显示剩余 mm:ss，确认阶段显示中止提示。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        SystemLang_t lang = appManager.getLanguage();

        if (phase == 3) {
            // 【危险操作界面】
            UIFrame::DrawDangerConfirm(UIStrings::DangerTitle(lang),
                                       UIStrings::CountdownAbortTitle(lang),
                                       UIStrings::CountdownAbortHint(lang));
        }
        else {
            // 【机甲引擎主界面】运行态只保留“执行中”状态，设置流程不再挤占运行页。
            if (phase == 2)
                linkAnim.drawCompact(UITheme::EditFlow::LinkY(), UIStrings::CountdownRunningLabel(lang), 85);
            else
                linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::CountdownStepNames(lang), 3, phase, 85);

            // 中部弧面滚轮；顶部步骤导轨已承担分隔，不重复绘制粗线。
            if (phase == 0) {
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), m, 0, 99, "");
            } else if (phase == 1) {
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), s, 0, 59, "");
            } else if (phase == 2) {
                const auto snapshot = Countdown_GetSnapshot();
                const int remain = Countdown_IsActive() ? Countdown_GetRemainingSeconds() : 0;
                char t_buf[16];
                snprintf(t_buf, sizeof(t_buf), "%02d:%02d", remain / 60, remain % 60);
                const int text_w = HAL_Get_Text_Width_Font(t_buf, HAL_FONT_TITLE);
                HAL_Screen_ShowLine_Font((sw - text_w) / 2, UITheme::Timer::ValueY(), t_buf,
                                         HAL_FONT_TITLE, TFT_CYAN);
                const float progress = snapshot.timer.total_ms == 0 ? 0.0f :
                    1.0f - (float)snapshot.timer.remaining_ms / snapshot.timer.total_ms;
                UIFrame::DrawTimerProgress(progress, UITheme::Timer::ProgressY(), TFT_RED);
            }

            // 4. 底部状态与操作指引 (Y=56)
            const char *tip;
            if (phase == 0 || phase == 1) {
                // 【沉浸式文案】：TT2 协议专属设定语
                tip = UIStrings::CountdownSetTip(lang);
            } else if (phase == 2) {
                tip = UIStrings::CountdownRunningTip(lang);
            } else {
                tip = UIStrings::CountdownAbortHint(lang);
            }

            UIFrame::DrawTip(tip);
        }
        HAL_Screen_Update();
    }

public:
    // 功能业务注册到已有 App 后台钩子，页面退出/被提醒替换后仍按主循环推进，不新增任务。
    void onSystemInit() override { appManager.registerBackgroundApp(this); }
    void onBackgroundTick() override { Countdown_Update(millis()); }

    // 【函数说明】进入倒计时页面：读取上次设置的分钟/秒数，初始化阶段为设置分钟并绘制界面。
    void onCreate() override {
        dialAnim.reset();
        // 进入页面只读取应用状态，已经到期但尚未入队的完成事实也会保留。
        const auto snapshot=Countdown_GetSnapshot();
        phase=snapshot.active ? 2 : 0;
        m=snapshot.active ? snapshot.minutes : 5;
        s=snapshot.active ? snapshot.seconds : 0;
        linkAnim.reset(phase);
        last_sec = -1;
        drawUI();
    }

    // 【函数说明】从其他页面返回时重绘 TT2 页面，保持剩余时间显示。
    void onResume() override { drawUI(); }

    void onLoop() override {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase == 2 ? 0 : phase);
        bool time_tick = false;

        // 【极客联动】：如果在设定时被外部接口强行拉起倒计时，UI 瞬间跃迁到执行模式
        if (Countdown_IsActive() && phase < 2) {
            phase = 2;
            linkAnim.jumpTo(2);
            drawUI();
        }

        if (phase == 2) {
            // 若被后台弹窗抢先处理完毕，或被外部取消，自动退栈
            if (!Countdown_IsActive()) {
                appManager.popApp();
                return;
            } else {
                int current_sec = Countdown_GetRemainingSeconds();
                if (current_sec != last_sec) {
                    last_sec = current_sec;
                    time_tick = true;
                }
            }
        }

        if (linkAnim.redrawDue(d_anim || l_anim || time_tick)) {
            drawUI();
        }
    }

    void onDestroy() override {}

    // 【函数说明】设置阶段旋钮调整分钟或秒钟，分钟按 1 递增，秒钟在 0-59 内循环。
    void onKnob(int delta) override {
        if (phase == 0) {
            m += delta;
            if (m < 0) m = 99;
            if (m > 99) m = 0;
            dialAnim.trigger(delta);
            linkAnim.triggerDial(delta);
            SYS_SOUND_GLITCH();
        }
        else if (phase == 1) {
            s += delta;
            if (s < 0) s = 59;
            if (s > 59) s = 0;
            dialAnim.trigger(delta);
            linkAnim.triggerDial(delta);
            SYS_SOUND_GLITCH();
        }
        // 调值只累计动画增量，由 onLoop 在帧节拍统一绘制，避免同轮重复推屏。
    }

    // 【函数说明】短按推进“分钟→秒钟→启动”；运行阶段短按打开中止确认，不直接清除到期事实。
    void onKeyShort() override {
        dialAnim.reset();
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1;
            drawUI();
        }
        else if (phase == 1) {
            if (m == 0 && s == 0) return; // 0秒防呆
            phase = 2;
            if (!Countdown_TryStart(m,s)) return;
            drawUI();
        }
        else if (phase == 2) {
            phase = 3; // 弹出“危险操作”确认撤收
            drawUI();
        }
        else if (phase == 3) {
            if (!Countdown_Cancel()) return;
            phase = 0; // 确认撤收，回到重设状态
            linkAnim.jumpTo(0);
            drawUI();
        }
    }

    // 【函数说明】长按在运行阶段进入中止确认，再次长按退出页面。
    void onKeyLong() override {
        dialAnim.reset();
        if (phase == 3) {
            SYS_SOUND_NAV();
            phase = 2; // 取消撤收，返回倒计时
            drawUI();
        }
        else if (phase == 1) {
            SYS_SOUND_NAV();
            phase = 0; // 取消设秒，返回设分
            drawUI();
        }
        else {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

AppCountdown instanceCountdown;
AppBase *appCountdown = &instanceCountdown;
