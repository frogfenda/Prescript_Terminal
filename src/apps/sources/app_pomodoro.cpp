/*
【模块职责】番茄钟协议。管理 5 组预设、工作/休息运行页和编辑页，网页 POM 命令可更新预设。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_pomodoro.cpp
#include "sys/app_base.h"
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "ui/ui_frame.h"
#include "sys/sys_config.h"
#include "sys/sys_event.h"
#include "sys/sys_ble.h"
#include "sys/sys_command_result.h"
#include "sys/sys_constants.h"
#include "sys/sys_reminder.h"
#include "sys/sys_sleep_scheduler.h"
#include "lang/ui_strings.h"
#include <limits.h>

// 【函数说明】处理 POM 命令：校验槽位和时长后更新对应番茄钟预设名称、工作分钟和休息分钟，并保存配置。
void Pomodoro_UpdatePreset(int index, const char* name, int work_m, int rest_m) {
    if (index < 0 || index >= PrescriptConst::MAX_POMODORO_PRESETS)
    {
        SysCmdResult_Error("INVALID_SLOT");
        return;
    }
    if (work_m <= 0 || rest_m <= 0)
    {
        SysCmdResult_Error("INVALID_DURATION");
        return;
    }

    String safeName = String(name);
    safeName.trim();
    if (safeName.length() == 0)
        safeName = "PRESET";

    sysConfig.pomodoro_presets[index].name = safeName;
    sysConfig.pomodoro_presets[index].work_min = work_m;
    sysConfig.pomodoro_presets[index].rest_min = rest_m;
    sysConfig.save();
    SysCmdResult_Ok("UPDATED", String(index));
}

// 【函数说明】事件总线回调：把 Router 解析出的番茄预设参数交给 Pomodoro_UpdatePreset。
void _Cb_PomUpd(void* payload);
// 【函数说明】WebBLE 同步回调：把番茄预设逐条打包成 SYNC:POM JSON 文本回传上位机。
void _Cb_PomSync(void* payload);

namespace
{
    enum class PomodoroPhase : uint8_t
    {
        Work = 0,
        Rest,
    };

    /**
     * 番茄钟运行状态属于功能本身，不属于运行页面。
     * AppManager 进入 Standby 时会销毁当前页面，但本结构继续由已安装的 Pomodoro 后台 tick 维护。
     */
    struct PomodoroRuntime
    {
        bool active = false;
        bool paused = false;
        PomodoroPhase phase = PomodoroPhase::Work;
        uint32_t deadline_ms = 0;
        uint32_t paused_remaining_ms = 0;
        PomodoroPreset preset;
    };

    PomodoroRuntime s_runtime;
    uint32_t s_last_background_check_ms = 0;

    uint32_t PomodoroDurationMs(uint32_t minutes)
    {
        uint64_t duration_ms = (uint64_t)minutes * 60ULL * 1000ULL;
        return duration_ms > (uint64_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)duration_ms;
    }

    bool PomodoroDeadlineReached(uint32_t now)
    {
        return (int32_t)(now - s_runtime.deadline_ms) >= 0;
    }

    uint32_t PomodoroRemainingMs(uint32_t now = millis())
    {
        if (!s_runtime.active)
            return 0;
        if (s_runtime.paused)
            return s_runtime.paused_remaining_ms;
        return PomodoroDeadlineReached(now) ? 0 : (uint32_t)(s_runtime.deadline_ms - now);
    }

    void PomodoroScheduleRemaining(uint32_t remaining_ms)
    {
        s_runtime.deadline_ms = millis() + remaining_ms;
        SysSleep_ScheduleAfterMs(
            SysSleepSource::Pomodoro,
            remaining_ms,
            SysSleepWakeAction::Foreground);
    }

    /** 从当前预设启动工作阶段；所有入口共用这一处建立运行状态和休眠截止时间。 */
    void PomodoroStart()
    {
        s_runtime.preset = sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx];
        s_runtime.active = true;
        s_runtime.paused = false;
        s_runtime.phase = PomodoroPhase::Work;
        s_runtime.paused_remaining_ms = 0;
        PomodoroScheduleRemaining(PomodoroDurationMs(s_runtime.preset.work_min));
    }

    void PomodoroPause()
    {
        if (!s_runtime.active || s_runtime.paused)
            return;
        s_runtime.paused_remaining_ms = PomodoroRemainingMs();
        s_runtime.paused = true;
        SysSleep_Cancel(SysSleepSource::Pomodoro);
    }

    void PomodoroResume()
    {
        if (!s_runtime.active || !s_runtime.paused)
            return;
        s_runtime.paused = false;
        PomodoroScheduleRemaining(s_runtime.paused_remaining_ms);
        s_runtime.paused_remaining_ms = 0;
    }

    /** 长按工作阶段时立即切入运行中的休息阶段，并把提示放入统一提醒队列。 */
    void PomodoroForceRest()
    {
        if (!s_runtime.active)
            return;
        s_runtime.phase = PomodoroPhase::Rest;
        s_runtime.paused = false;
        s_runtime.paused_remaining_ms = 0;
        PomodoroScheduleRemaining(PomodoroDurationMs(s_runtime.preset.rest_min));
        SysReminder_Submit(
            SysReminderKind::Custom,
            UIStrings::PomodoroForceRestPrescript(appManager.getLanguage()),
            true);
    }

    void PomodoroStop()
    {
        s_runtime.active = false;
        s_runtime.paused = false;
        s_runtime.paused_remaining_ms = 0;
        SysSleep_Cancel(SysSleepSource::Pomodoro);
    }

    /**
     * 主循环后台检查阶段截止时间。
     * 只有提醒成功入队后才推进状态；队列满时保留已经到期的截止时间，下一秒继续重试，避免静默丢失阶段通知。
     */
    void PomodoroUpdate()
    {
        uint32_t now = millis();
        if ((uint32_t)(now - s_last_background_check_ms) < 1000)
            return;
        s_last_background_check_ms = now;

        if (!s_runtime.active || s_runtime.paused || !PomodoroDeadlineReached(now))
            return;

        if (s_runtime.phase == PomodoroPhase::Work)
        {
            if (!SysReminder_Submit(
                    SysReminderKind::Custom,
                    UIStrings::PomodoroWorkDonePrescript(appManager.getLanguage()),
                    true))
                return;

            /* 工作结束后沿用原交互：先提醒，再把休息阶段停在暂停态等待用户确认开始。 */
            s_runtime.phase = PomodoroPhase::Rest;
            s_runtime.paused = true;
            s_runtime.paused_remaining_ms = PomodoroDurationMs(s_runtime.preset.rest_min);
            SysSleep_Cancel(SysSleepSource::Pomodoro);
            return;
        }

        if (!SysReminder_Submit(
                SysReminderKind::Custom,
                UIStrings::PomodoroRestDonePrescript(appManager.getLanguage()),
                false))
            return;
        PomodoroStop();
    }
}

class AppPomodoroRun : public AppBase {
private:
    uint32_t last_sec_draw;

    // 【函数说明】绘制番茄钟运行页：显示当前工作/休息阶段、剩余时间和暂停状态。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int center_y = 30;
        
        HAL_Screen_ShowChineseLine(10, center_y, s_runtime.preset.name.c_str());
        
        uint32_t remain = PomodoroRemainingMs();
        
        char time_buf[16];
        sprintf(time_buf, "%02d:%02d", remain / 60000, (remain / 1000) % 60);
        int tw = HAL_Get_Text_Width(time_buf);
        HAL_Screen_ShowTextLine(sw - tw - 10, center_y, time_buf);
        
        SystemLang_t lang = appManager.getLanguage();
        const char* state_str = s_runtime.paused
            ? UIStrings::PomodoroPausedStatus(lang)
            : UIStrings::PomodoroPhaseStatus(lang, (int)s_runtime.phase);
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(state_str))/2, 56, state_str, 0.4f);
        
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入运行页时读取当前预设，启动工作阶段计时并绘制第一帧。
    void onCreate() override {
        if (!s_runtime.active)
            PomodoroStart();
        last_sec_draw = 0xFFFFFFFF;
        drawUI();
    }
    
    void onResume() override { drawUI(); }

    // 【函数说明】每秒更新剩余时间，工作到点后切休息，休息到点后提示完成并返回待启动状态。
    void onLoop() override {
        if (s_runtime.active && !s_runtime.paused) {
            uint32_t current_sec = PomodoroRemainingMs() / 1000;
            if (current_sec != last_sec_draw) {
                last_sec_draw = current_sec;
                drawUI(); 
            }
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    // 【函数说明】短按暂停或继续番茄钟计时。
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (s_runtime.paused)
            PomodoroResume();
        else
            PomodoroPause();
        drawUI();
    }

    // 【函数说明】长按结束本次番茄钟并返回预设菜单。
    void onKeyLong() override {
        SYS_SOUND_NAV();
        if (s_runtime.phase == PomodoroPhase::Work) {
            last_sec_draw = 0xFFFFFFFF;
            PomodoroForceRest();
        } else {
            PomodoroStop();
            appManager.popApp();
        }
    }
};
AppPomodoroRun instancePomodoroRun; AppBase* appPomodoroRun = &instancePomodoroRun;

class AppPomodoroEdit : public AppBase {
private:
    int phase; 
    int t_work;
    int t_rest;
    PomodoroPreset* p_preset;

    DialAnimator dialAnim;        // 实例化刻度盘引擎
    TacticalLinkEngine linkAnim;  // 实例化战术链路引擎

    // 【函数说明】绘制番茄预设编辑器：流程链路显示名称/工作/休息，滚轮调整工作和休息分钟。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        SystemLang_t lang = appManager.getLanguage();

        // 1. 顶部战术链路区
        linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::PomodoroEditStepNames(lang), 2, phase, 120);

        // 2. 中间机甲分隔线
        UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

        // 3. 底部动态机械刻度盘
        if (phase == 0) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_work, 1, 120, "");
        else dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_rest, 1, 60, "");

        // 4. 底部状态与操作指引
        UIFrame::DrawTip(UIStrings::PomodoroEditTip(lang));
        
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入预设编辑页时读取当前预设参数并重置编辑阶段。
    void onCreate() override {
        p_preset = &sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx];
        t_work = p_preset->work_min;
        t_rest = p_preset->rest_min;
        phase = 0;
        linkAnim.jumpTo(phase);
        drawUI();
    }
    void onResume() override { drawUI(); }
    
    void onLoop() override { 
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if(d_anim || l_anim) drawUI();
    }
    
    void onDestroy() override {}
    
    // 【函数说明】旋钮调整工作分钟或休息分钟，并触发数字滚轮动画。
    void onKnob(int delta) override {
        if (phase == 0) {
            t_work += delta;
            if (t_work < 1) t_work = 120; 
            if (t_work > 120) t_work = 1;
        } else {
            t_rest += delta;
            if (t_rest < 1) t_rest = 60;
            if (t_rest > 60) t_rest = 1;
        }
        
        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    // 【函数说明】短按推进编辑阶段；最后一阶段写回 sysConfig.pomodoro_presets 并保存。
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1;
            drawUI();
        } else {
            p_preset->work_min = t_work;
            p_preset->rest_min = t_rest;
            sysConfig.save();
            appManager.popApp();
        }
    }
    
    // 【函数说明】长按放弃编辑并返回预设列表。
    void onKeyLong() override {
        SYS_SOUND_NAV();
        if (phase == 1) {
            phase = 0;
            drawUI();
        } else {
            appManager.popApp();
        }
    }
};
AppPomodoroEdit instancePomodoroEdit; AppBase* appPomodoroEdit = &instancePomodoroEdit;

class AppPomodoroPresets : public AppMenuBase {
protected:
    int getMenuCount() override { return 5; }
    const char* getTitle() override { return UIStrings::PomodoroPresetTitle(appManager.getLanguage()); }
    const char* getItemText(int index) override {
        static char buf[64];
        PomodoroPreset& p = sysConfig.pomodoro_presets[index];
        const char* mark = (index == sysConfig.pomodoro_current_idx) ? " <" : "";
        sprintf(buf, "%s (%d/%d)%s", p.name.c_str(), p.work_min, p.rest_min, mark);
        return buf;
    }
    // 【函数说明】选择某个预设作为当前番茄钟配置，并进入运行页。
    void onItemClicked(int index) override {
        sysConfig.pomodoro_current_idx = index;
        sysConfig.save(); 
        appManager.popApp(); 
    }
    void onLongPressed() override { appManager.popApp(); }
};
AppPomodoroPresets instancePomodoroPresets; AppBase* appPomodoroPresets = &instancePomodoroPresets;

class AppPomodoroMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 3; } 
    const char* getTitle() override { return UIStrings::PomodoroTitle(appManager.getLanguage()); }
    const char* getItemText(int index) override {
        static char buf[64];
        SystemLang_t lang = appManager.getLanguage();
        if (index == 0) {
            snprintf(buf, sizeof(buf), "%s [%s]", UIStrings::PomodoroRunLabel(lang), sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx].name.c_str());
            return buf;
        }
        if (index == 1 || index == 2)
            return UIStrings::PomodoroMenuItem(lang, index);
        return "";
    }
    // 【函数说明】番茄主菜单点击入口：开始当前预设、选择预设、编辑预设。
    void onItemClicked(int index) override {
        if (index == 0) appManager.push(AppId::PomodoroRun);
        if (index == 1) appManager.push(AppId::PomodoroPresets);
        if (index == 2) appManager.push(AppId::PomodoroEdit);
    }
    void onLongPressed() override { appManager.popApp(); }
public:
    void onResume() override { AppMenuBase::onResume(); }
    // 【函数说明】订阅 POMODORO_UPDATE 事件，让网页 POM 命令能更新预设。
    void onSystemInit() override{
    SysEvent_Subscribe(EVT_POMODORO_UPDATE, _Cb_PomUpd);
    SysEvent_Subscribe(EVT_BLE_SYNC_REQ, _Cb_PomSync);
    appManager.registerBackgroundApp(this);
}
    /** 番茄运行页即使被 Standby 销毁，功能级状态仍在这里按秒推进并提交统一提醒。 */
    void onBackgroundTick() override { PomodoroUpdate(); }
};
// === 番茄钟专属拆包回调 ===
// 【函数说明】事件总线回调：把 Router 解析出的番茄预设参数交给 Pomodoro_UpdatePreset。
void _Cb_PomUpd(void* payload) {
    Evt_PomUpd_t* p = (Evt_PomUpd_t*)payload;
    // 呼叫原本用于更新的接口
    // 【函数说明】处理 POM 命令：校验槽位和时长后更新对应番茄钟预设名称、工作分钟和休息分钟，并保存配置。
    extern void Pomodoro_UpdatePreset(int idx, const char* name, int w, int r);
    Pomodoro_UpdatePreset(p->idx, p->name, p->w, p->r);
}

// 【函数说明】WebBLE 同步回调：只在全量同步或 POM 同步域下回传番茄预设。
void _Cb_PomSync(void* payload) {
    if (!SysEvent_BleSyncScopeMatches(payload, "POM"))
        return;

    for (int i = 0; i < PrescriptConst::MAX_POMODORO_PRESETS; i++) {
        String safeName = sysConfig.pomodoro_presets[i].name.c_str();
        safeName.replace("\\", "\\\\");
        safeName.replace("\"", "\\\"");
        String out = "SYNC:POM:{\"slot\":" + String(i) +
                     ",\"n\":\"" + safeName +
                     "\",\"w\":" + String(sysConfig.pomodoro_presets[i].work_min) +
                     ",\"r\":" + String(sysConfig.pomodoro_presets[i].rest_min) +
                     ",\"cur\":" + String(sysConfig.pomodoro_current_idx) + "}";
        SysBLE_Notify(out.c_str());
        delay(20);
    }
}

// 供系统开机时调用的注册入口

AppPomodoroMenu instancePomodoroMenu; AppBase* appPomodoro = &instancePomodoroMenu;
