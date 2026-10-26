/*
【模块职责】番茄钟应用业务、页面与协议适配；后台 tick 维护阶段和提醒，复用系统计时内核。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_pomodoro.cpp
#include "sys/app_base.h"
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "ui/ui_frame.h"
#include "sys/sys_command_registry.h"
#include "sys/sys_psram_json.h"
#include "sys/sys_constants.h"

// 阶段流转、运行快照和预设属于本 App；共享的计时、提醒、唤醒机制继续复用 SYS。
// 注册 POM 命令组；定义在文件末尾的【蓝牙命令区】。
static void Pomodoro_RegisterCommands();

// 【计时业务开始】
/*
【模块职责】番茄阶段、预设更新和完成提醒；持续时长算法仅复用 SysDurationTimer。
【重要约束】主循环调用；专注到点自动开始休息，休息到点结束本轮，页面离开不改变计时。
提醒失败不回滚阶段；两种阶段事实各自保留并重试，取消会话时清除尚未展示的旧提示。
*/
#include "sys/sys_duration_timer.h"
#include "sys/sys_clock_rules.h"
#include "sys/sys_config.h"
#include "sys/sys_command_result.h"
#include "sys/sys_reminder.h"
#include "sys/sys_sleep_scheduler.h"
#include "lang/ui_strings.h"
enum class PomodoroPhase : uint8_t { Work, Rest };
struct PomodoroSnapshot
{
    SysDurationSnapshot timer;
    PomodoroPhase phase;
    bool active, completion_pending, waiting_for_next, set_completed;
    uint32_t round, rounds, revision, session;
    const char *name; // 借用应用快照名称，仅当帧使用；下一次 Start 后可能失效，不逐帧复制长字符串。
};
namespace {
SysDurationTimer timer;
PomodoroPreset preset;
PomodoroPhase phase=PomodoroPhase::Work;
bool active=false,work_notice=false,rest_notice=false;
bool waiting_for_next=false,set_completed=false;
uint32_t session_round=0,session=0,revision=0;
int session_preset_index=-1;
void Schedule(uint32_t now)
{ SysSleep_ScheduleDeadlineMs(SysSleepSource::Pomodoro,timer.snapshot(now).deadline_ms,SysSleepWakeAction::Foreground); }
void FlushNotices()
{
    const auto lang=TerminalLang::Normalize(sysConfig.language);
    const uint64_t key=(uint64_t)session<<8;
    // 自然专注完成保留当前页面返回关系，休息结束则按整轮完成明确中断；手动提前结束由确认页说明。
    if (work_notice && SysReminder_SubmitClock(SysClockReminderSource::Pomodoro,key|1,
            SysReminderPresentation::ReturnToPrevious,SysReminderKind::Custom,
            UIStrings::PomodoroWorkDonePrescript(lang)))
        { work_notice=false; ++revision; }
    if (!work_notice && rest_notice && SysReminder_SubmitClock(SysClockReminderSource::Pomodoro,key|2,
            SysReminderPresentation::Interrupt,SysReminderKind::Custom,UIStrings::PomodoroRestDonePrescript(lang)))
        { rest_notice=false; ++revision; }
}
void RecordFocusCompletion()
{
    const int index=session_preset_index;
    if (index<0 || index>=PrescriptConst::MAX_POMODORO_PRESETS) return;
    auto &saved=sysConfig.pomodoro_presets[index];
    if (saved.completed_rounds!=UINT32_MAX) ++saved.completed_rounds;
    const uint32_t seconds=preset.work_min*60U;
    saved.focus_seconds=(UINT32_MAX-saved.focus_seconds<seconds)?UINT32_MAX:saved.focus_seconds+seconds;
    sysConfig.save();
}
}
/**
 * 【专注业务入口】修改一个预设槽位；本机编辑器和蓝牙 POM.SET 共用。
 * rounds 小于 1 表示保持原轮数；名称为空时使用 "PRESET"。
 * 结果码：UPDATED / INVALID_SLOT / INVALID_DURATION / INVALID_ROUNDS。
 */
SysOpResult Pomodoro_SetPreset(int index,const char *name,int work,int rest,int rounds=-1)
{
    if (index<0 || index>=PrescriptConst::MAX_POMODORO_PRESETS) return SysOpResult::Error("INVALID_SLOT");
    if (!SysClockRules::PomodoroDuration(work,rest)) return SysOpResult::Error("INVALID_DURATION");
    String safe=name ? name : ""; safe.trim(); if (safe.isEmpty()) safe="PRESET";
    auto &target=sysConfig.pomodoro_presets[index];
    const int safe_rounds=rounds<1 ? (int)target.rounds : rounds;
    if (!SysClockRules::PomodoroRounds(safe_rounds)) return SysOpResult::Error("INVALID_ROUNDS");
    target.name=safe; target.work_min=work; target.rest_min=rest; target.rounds=safe_rounds;
    sysConfig.save();
    SysCommand_NotifyChanged("POM");
    return SysOpResult::Ok("UPDATED",String(index));
}

bool Pomodoro_SelectPreset(int index)
{
    if (index<0 || index>=PrescriptConst::MAX_POMODORO_PRESETS) return false;
    if (sysConfig.pomodoro_current_idx!=index) {
        sysConfig.pomodoro_current_idx=index;
        sysConfig.save();
    }
    return true;
}
bool Pomodoro_Start()
{
    if (active || work_notice || rest_notice) return false;
    const int index=sysConfig.pomodoro_current_idx;
    if (index>=PrescriptConst::MAX_POMODORO_PRESETS) return false;
    const auto &selected=sysConfig.pomodoro_presets[index];
    if (!SysClockRules::PomodoroDuration(selected.work_min,selected.rest_min) || !SysClockRules::PomodoroRounds(selected.rounds)) return false;
    SysReminder_CancelClock(SysClockReminderSource::Pomodoro);
    preset=selected; phase=PomodoroPhase::Work; active=true; waiting_for_next=false; set_completed=false; session_round=0;
    session_preset_index=index;
    if (++session==0) ++session;
    ++revision;
    const uint32_t now=millis(); timer.start(preset.work_min*60000U,now); Schedule(now); return true;
}
bool Pomodoro_StartNextRound()
{
    if (active || !waiting_for_next || work_notice || rest_notice || session_round>=preset.rounds) return false;
    waiting_for_next=false; set_completed=false; phase=PomodoroPhase::Work; active=true; ++revision;
    const uint32_t now=millis(); timer.start(preset.work_min*60000U,now); Schedule(now); return true;
}
void Pomodoro_Update(uint32_t now);
/** 等待下一轮时结束整组；已经自然完成的专注轮次和统计不会被清除。 */
bool Pomodoro_EndSet()
{
    const uint32_t now=millis();
    Pomodoro_Update(now);
    timer.cancel(now); active=false; waiting_for_next=false; set_completed=true;
    work_notice=rest_notice=false;
    SysSleep_Cancel(SysSleepSource::Pomodoro);
    SysReminder_CancelClock(SysClockReminderSource::Pomodoro);
    ++revision;
    return true;
}
void Pomodoro_Update(uint32_t now)
{
    // 一轮最多跨专注/休息两个边界；休息锚定专注的真实截止点，后台迟到不会凭空延长本轮。
    for (int transition=0;transition<2 && active && timer.advance(now);++transition) {
        const uint32_t completed_at=timer.snapshot(now).deadline_ms;
        timer.acknowledge(); SysSleep_Cancel(SysSleepSource::Pomodoro); ++revision;
        if (phase==PomodoroPhase::Work) {
            if (session_round<preset.rounds) ++session_round;
            RecordFocusCompletion();
            phase=PomodoroPhase::Rest; work_notice=true;
            timer.start(preset.rest_min*60000U,completed_at); Schedule(now);
        } else { active=false; waiting_for_next=session_round<preset.rounds; set_completed=!waiting_for_next; rest_notice=true; }
    }
    FlushNotices();
}
/** 确认页携带会话与阶段；先结算自然到点，再核对目标，旧确认不得作用到休息或下一轮。 */
bool Pomodoro_FinishPhase(uint32_t expected_session,PomodoroPhase expected_phase)
{
    const uint32_t now=millis(); Pomodoro_Update(now);
    if (!active || session!=expected_session || phase!=expected_phase) return false;
    ++revision;
    if (phase==PomodoroPhase::Work) {
        phase=PomodoroPhase::Rest;
        timer.start(preset.rest_min*60000U,now); Schedule(now); return true;
    }
    timer.cancel(now); active=false; waiting_for_next=session_round<preset.rounds; set_completed=!waiting_for_next; work_notice=rest_notice=false;
    SysSleep_Cancel(SysSleepSource::Pomodoro); SysReminder_CancelClock(SysClockReminderSource::Pomodoro); return true;
}
PomodoroSnapshot Pomodoro_GetSnapshot()
{
    const auto state=timer.snapshot(millis());
    return {state,phase,active,work_notice||rest_notice,waiting_for_next,set_completed,session_round,preset.rounds,revision,session,preset.name.c_str()};
}
/** 主循环 HUD 查询只复制两个小字段；业务仍归本 App，不把阶段状态迁入 SYS。 */
bool Pomodoro_GetHudStatus(bool *rest,int *remaining_seconds)
{
    const auto snapshot=Pomodoro_GetSnapshot();
    if (rest) *rest=snapshot.phase==PomodoroPhase::Rest;
    if (remaining_seconds) *remaining_seconds=snapshot.active ? (int)SysDurationDisplaySeconds(snapshot.timer.remaining_ms) : 0;
    return snapshot.active;
}
// 【计时业务结束】

// 【运行页绘制开始】
// 复用倒计时的两段导轨、红绳和底部提示；读数与预设名称按 428×142 的剩余区域分行排布。
void Pomodoro_DrawRunning(TacticalLinkEngine &link,SystemLang_t lang,int phase,uint32_t total_ms,uint32_t remaining_ms,const char *name,int name_width,uint32_t round,uint32_t rounds)
{
    link.draw(UITheme::EditFlow::LinkY(),UIStrings::PomodoroRunSteps(lang),2,phase,110);
    const int screen_w=HAL_Get_Screen_Width();
    const int time_y=UITheme::Timer::ValueY();
    const int name_y=UITheme::Timer::NameY();
    // 预设可由手机写入长文本；先按完整 UTF-8 字符压缩成省略号，避免长名称变成一整行密集字形。
    String display_name = name ? name : "";
    const int name_limit = screen_w - 64;
    if (name_width > name_limit)
    {
        String shortened;
        int pos = 0;
        while (display_name[pos] != '\0')
        {
            const unsigned char c = (unsigned char)display_name[pos];
            int len = (c & 0x80) == 0 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
            String candidate = shortened;
            for (int i = 0; i < len && display_name[pos + i] != '\0'; ++i) candidate += display_name[pos + i];
            candidate += "...";
            if (HAL_Get_Text_Width(candidate.c_str()) > name_limit) break;
            for (int i = 0; i < len && display_name[pos + i] != '\0'; ++i) shortened += display_name[pos + i];
            pos += len;
        }
        shortened += "...";
        display_name = shortened;
        name_width = HAL_Get_Text_Width(display_name.c_str());
    }
    UIText::DrawClippedFaded(max(24,(screen_w-name_width)/2),name_y,display_name.c_str(),0.55f,24,screen_w-24);
    const int seconds=(int)SysDurationDisplaySeconds(remaining_ms);
    char time[20]; snprintf(time,sizeof(time),"%02d:%02d",seconds/60,seconds%60);
    const int time_w=HAL_Get_Text_Width_Font(time,HAL_FONT_TITLE);
    HAL_Screen_ShowLine_Font((screen_w-time_w)/2,time_y,time,HAL_FONT_TITLE,TFT_CYAN);
    const float progress=total_ms==0 ? 0.0f : 1.0f-(float)remaining_ms/total_ms;
    UIFrame::DrawTimerProgress(progress,UITheme::Timer::ProgressY(),phase==0 ? TFT_RED : UITheme::COLOR_WARN);
    UIFrame::DrawRoundDots(round,rounds,UITheme::Timer::MetaY());
    UIFrame::DrawTip(UIStrings::PomodoroRunningTip(lang,phase));
}
// 【运行页绘制结束】

class AppPomodoroRun : public AppBase {
    TacticalLinkEngine link;
    bool confirming=false;
    uint32_t confirm_session=0;
    PomodoroPhase confirm_phase=PomodoroPhase::Work;
    uint32_t last_sec=0xffffffffU,last_revision=0;
    int name_width=0;

    bool confirmationCurrent(const PomodoroSnapshot &snapshot) const
    {
        return snapshot.active && snapshot.session==confirm_session && snapshot.phase==confirm_phase;
    }
    void drawUI()
    {
        const auto snapshot=Pomodoro_GetSnapshot();
        if (confirming && !confirmationCurrent(snapshot)) confirming=false;
        HAL_Sprite_Clear();
        const auto lang=appManager.getLanguage();
        if (confirming) {
            UIFrame::DrawDangerConfirm(UIStrings::DangerTitle(lang),
                UIStrings::PomodoroFinishTitle(lang,(int)confirm_phase),UIStrings::PomodoroFinishHint(lang));
        } else if (!snapshot.active) {
            link.draw(UITheme::EditFlow::LinkY(),UIStrings::PomodoroRunSteps(lang),2,snapshot.set_completed ? 1 : 0,110);
            UIText::DrawCentered(UITheme::Timer::ValueY(),UIStrings::PomodoroWaitingLabel(lang,snapshot.set_completed));
            UIFrame::DrawRoundDots(snapshot.round,snapshot.rounds,UITheme::Timer::MetaY());
            UIFrame::DrawTip(UIStrings::PomodoroWaitingTip(lang,snapshot.set_completed));
        } else {
            Pomodoro_DrawRunning(link,lang,(int)snapshot.phase,snapshot.timer.total_ms,snapshot.timer.remaining_ms,
                                 snapshot.name,name_width,snapshot.round,snapshot.rounds);
        }
        last_sec=SysDurationDisplaySeconds(snapshot.timer.remaining_ms); last_revision=snapshot.revision;
        HAL_Screen_Update();
    }
public:
    // 再次进入只恢复当前会话；真正空闲时才开始所选预设，队列满的完成事实不得被新会话抹掉。
    void onCreate() override
    {
        auto snapshot=Pomodoro_GetSnapshot();
        if (!snapshot.active && !snapshot.completion_pending && !snapshot.waiting_for_next && !snapshot.set_completed) {
            Pomodoro_Start(); snapshot=Pomodoro_GetSnapshot();
        }
        confirming=false; link.reset((int)snapshot.phase);
        name_width=HAL_Get_Text_Width(snapshot.name);
        drawUI();
    }
    void onResume() override { drawUI(); }
    void onDestroy() override {} // 页面退出不停止业务；AppPomodoroMenu 的后台 tick 持续推进。
    void onKnob(int) override {}
    void onLoop() override
    {
        const auto snapshot=Pomodoro_GetSnapshot();
        bool changed=snapshot.revision!=last_revision || SysDurationDisplaySeconds(snapshot.timer.remaining_ms)!=last_sec;
        if (!snapshot.active) { if (changed) drawUI(); return; }
        if (confirming) {
            if (confirmationCurrent(snapshot)) return; // 确认期间照常计时，静态弹窗不跟着秒数反复推屏。
            confirming=false; changed=true;
        }
        const bool moving=link.update((int)snapshot.phase);
        if (link.redrawDue(moving || changed)) drawUI();
    }
    // 第一次短按只记住确认目标；第二次才请求结束该阶段，业务入口还会再次校验会话和阶段。
    void onKeyShort() override
    {
        Pomodoro_Update(millis());
        const auto snapshot=Pomodoro_GetSnapshot();
        if (!snapshot.active) {
            if (snapshot.waiting_for_next) Pomodoro_StartNextRound();
            else if (snapshot.set_completed) Pomodoro_Start();
            drawUI(); return;
        }
        SYS_SOUND_CONFIRM();
        if (!confirming) {
            confirm_session=snapshot.session; confirm_phase=snapshot.phase; confirming=true;
        } else {
            confirming=false;
            if (confirmationCurrent(snapshot) && Pomodoro_FinishPhase(confirm_session,confirm_phase)) {
                const auto current=Pomodoro_GetSnapshot();
                if (!current.active) { drawUI(); return; }
                link.jumpTo((int)current.phase);
            }
        }
        drawUI();
    }
    // 确认页长按取消；运行中长按退出界面并保留后台计时；休息结束等待时长按结束整组。
    void onKeyLong() override
    {
        SYS_SOUND_NAV();
        if (confirming) { confirming=false; drawUI(); }
        else {
            const auto snapshot=Pomodoro_GetSnapshot();
            if (!snapshot.active && snapshot.waiting_for_next) Pomodoro_EndSet();
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
    int t_rounds;
    int preset_index;

    DialAnimator dialAnim;        // 实例化刻度盘引擎
    TacticalLinkEngine linkAnim;  // 实例化战术链路引擎

    // 【函数说明】绘制番茄预设编辑器：流程链路显示名称/工作/休息，滚轮调整工作和休息分钟。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        SystemLang_t lang = appManager.getLanguage();

        // 1. 顶部战术链路区
        linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::PomodoroEditStepNames(lang), 3, phase, 120);

        // 中部弧面滚轮；位置与底部提示共同分配剩余高度。
        if (phase == 0) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_work, 1, SysClockRules::WorkMaxMinutes, "");
        else if (phase == 1) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_rest, 1, SysClockRules::RestMaxMinutes, "");
        else dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_rounds, 1, SysClockRules::PomodoroMaxRounds, "");

        // 4. 底部状态与操作指引
        UIFrame::DrawTip(UIStrings::PomodoroEditTip(lang));

        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入预设编辑页时读取当前预设参数并重置编辑阶段。
    void onCreate() override {
        dialAnim.reset();
        preset_index = sysConfig.pomodoro_current_idx;
        if (preset_index>=PrescriptConst::MAX_POMODORO_PRESETS) preset_index=0;
        const auto &preset=sysConfig.pomodoro_presets[preset_index];
        t_work = preset.work_min;
        t_rest = preset.rest_min;
        t_rounds = SysClockRules::PomodoroRounds(preset.rounds) ? preset.rounds : 4;
        phase = 0;
        linkAnim.reset(phase);
        drawUI();
    }
    void onResume() override { drawUI(); }

    void onLoop() override {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if(linkAnim.redrawDue(d_anim || l_anim)) drawUI();
    }

    void onDestroy() override {}

    // 【函数说明】旋钮调整工作分钟或休息分钟，并触发数字滚轮动画。
    void onKnob(int delta) override {
        if (phase == 0) {
            t_work += delta;
            if (t_work < 1) t_work = SysClockRules::WorkMaxMinutes;
            if (t_work > SysClockRules::WorkMaxMinutes) t_work = 1;
        } else if (phase == 1) {
            t_rest += delta;
            if (t_rest < 1) t_rest = SysClockRules::RestMaxMinutes;
            if (t_rest > SysClockRules::RestMaxMinutes) t_rest = 1;
        } else {
            t_rounds += delta;
            if (t_rounds < 1) t_rounds = SysClockRules::PomodoroMaxRounds;
            if (t_rounds > SysClockRules::PomodoroMaxRounds) t_rounds = 1;
        }

        dialAnim.trigger(delta);
        linkAnim.triggerDial(delta);
        SYS_SOUND_GLITCH();
        // 调值只累计动画增量，由 onLoop 在帧节拍统一绘制，避免同轮重复推屏。
    }

    // 【函数说明】短按推进编辑阶段；最后复用本 App 的预设校验入口保存时长。
    void onKeyShort() override {
        dialAnim.reset();
        SYS_SOUND_CONFIRM();
        if (phase < 2) {
            ++phase;
            drawUI();
        } else {
            if (!Pomodoro_SetPreset(preset_index,sysConfig.pomodoro_presets[preset_index].name.c_str(),t_work,t_rest,t_rounds).ok()) return;
            appManager.popApp();
        }
    }

    // 【函数说明】长按放弃编辑并返回预设列表。
    void onKeyLong() override {
        dialAnim.reset();
        SYS_SOUND_NAV();
        if (phase > 0) {
            --phase;
            drawUI();
        } else {
            appManager.popApp();
        }
    }
};
AppPomodoroEdit instancePomodoroEdit; AppBase* appPomodoroEdit = &instancePomodoroEdit;

class AppPomodoroPresets : public AppMenuBase {
protected:
    int getMenuCount() override { return PrescriptConst::MAX_POMODORO_PRESETS; }
    const char* getTitle() override { return UIStrings::PomodoroPresetTitle(appManager.getLanguage()); }
    const char* getItemText(int index) override {
        static String label;
        PomodoroPreset& p = sysConfig.pomodoro_presets[index];
        const char* mark = (index == sysConfig.pomodoro_current_idx) ? " <" : "";
        char suffix[40];
        snprintf(suffix,sizeof(suffix)," (%u/%u x%u)%s",(unsigned)p.work_min,(unsigned)p.rest_min,(unsigned)p.rounds,mark);
        label=p.name; label+=suffix; // 完整文本交给 AppMenuBase 的两行排版/省略，不先截断 UTF-8。
        return label.c_str();
    }
    // 【函数说明】选择某个预设作为当前番茄钟配置，并进入运行页。
    void onItemClicked(int index) override {
        if (!Pomodoro_SelectPreset(index)) return;
        appManager.popApp();
    }
    void onLongPressed() override { appManager.popApp(); }
};
AppPomodoroPresets instancePomodoroPresets; AppBase* appPomodoroPresets = &instancePomodoroPresets;

class AppPomodoroStats : public AppMenuBase {
protected:
    int getMenuCount() override { return 3; }
    const char *getTitle() override { return UIStrings::PomodoroStatsTitle(appManager.getLanguage()); }
    const char *getItemText(int index) override {
        static char line[96];
        const auto lang=appManager.getLanguage();
        const int selected=sysConfig.pomodoro_current_idx<PrescriptConst::MAX_POMODORO_PRESETS ? sysConfig.pomodoro_current_idx : 0;
        const auto &p=sysConfig.pomodoro_presets[selected];
        if (index==0) snprintf(line,sizeof(line),"%s: %u",UIStrings::PomodoroCompletedLabel(lang),(unsigned)p.completed_rounds);
        else if (index==1) { const uint32_t minutes=p.focus_seconds/60U; snprintf(line,sizeof(line),"%s: %u:%02u",UIStrings::PomodoroFocusTimeLabel(lang),(unsigned)(minutes/60U),(unsigned)(minutes%60U)); }
        else snprintf(line,sizeof(line),"%s [%s]",UIStrings::PomodoroStatsClear(lang),p.name.c_str());
        return line;
    }
    void onItemClicked(int index) override {
        if (index!=2) return;
        SYS_SOUND_GLITCH();
        const int selected=sysConfig.pomodoro_current_idx<PrescriptConst::MAX_POMODORO_PRESETS ? sysConfig.pomodoro_current_idx : 0;
        auto &p=sysConfig.pomodoro_presets[selected]; p.completed_rounds=0; p.focus_seconds=0; sysConfig.save();
    }
    void onLongPressed() override { SYS_SOUND_NAV(); appManager.popApp(); }
};
AppPomodoroStats instancePomodoroStats; AppBase* appPomodoroStats=&instancePomodoroStats;

class AppPomodoroMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 4; }
    const char* getTitle() override { return UIStrings::PomodoroTitle(appManager.getLanguage()); }
    const char* getItemText(int index) override {
        static String label;
        SystemLang_t lang = appManager.getLanguage();
        if (index == 0) {
            const auto snapshot=Pomodoro_GetSnapshot();
            const int selected=sysConfig.pomodoro_current_idx<PrescriptConst::MAX_POMODORO_PRESETS ? sysConfig.pomodoro_current_idx : 0;
            label=snapshot.active ? UIStrings::PomodoroResumeLabel(lang,(int)snapshot.phase) : UIStrings::PomodoroRunLabel(lang);
            label+=" ["; label+=snapshot.active ? snapshot.name : sysConfig.pomodoro_presets[selected].name.c_str(); label+="]";
            return label.c_str();
        }
        if (index >= 1 && index <= 3)
            return UIStrings::PomodoroMenuItem(lang, index);
        return "";
    }
    // 【函数说明】番茄主菜单点击入口：开始当前预设、选择预设、编辑预设。
    void onItemClicked(int index) override {
        if (index == 0) appManager.push(AppId::PomodoroRun);
        if (index == 1) appManager.push(AppId::PomodoroPresets);
        if (index == 2) appManager.push(AppId::PomodoroEdit);
        if (index == 3) appManager.push(AppId::PomodoroStats);
    }
    void onLongPressed() override { appManager.popApp(); }
public:
    void onResume() override { AppMenuBase::onResume(); }
    // 【函数说明】订阅 POMODORO_UPDATE 事件，让网页 POM 命令能更新预设。
    void onSystemInit() override {
        Pomodoro_RegisterCommands();
        appManager.registerBackgroundApp(this);
    }
    // 与前台页面生命周期无关；沿用 AppManager 在输入和提醒消费前的后台 tick。
    void onBackgroundTick() override { Pomodoro_Update(millis()); }
};
AppPomodoroMenu instancePomodoroMenu; AppBase* appPomodoro = &instancePomodoroMenu;

// ===== 【蓝牙命令区】POM 专注 =====
// 处理函数只把参数交给 Pomodoro_SetPreset，并把结果翻译成 ACK。

static const SysCmdArgSpec kPomSetArgs[] = {
    SYS_ARG_INT("slot", 0, PrescriptConst::MAX_POMODORO_PRESETS - 1),
    SYS_ARG_STR("name", 0, 24, ARG_TRIM),
    SYS_ARG_INT("work", 1, SysClockRules::WorkMaxMinutes),
    SYS_ARG_INT("rest", 1, SysClockRules::RestMaxMinutes),
    SYS_ARG_INT_OPT("rounds", 1, SysClockRules::PomodoroMaxRounds),
};

// POM.SET：修改一个预设；省略 rounds 时保持原轮数。
static void Cmd_PomSet(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(Pomodoro_SetPreset(args.num(0), args.str(1), args.num(2), args.num(3),
                               args.has(4) ? args.num(4) : -1));
}

// POM.LIST：第 index 个预设 → {"slot","n","w","r","rd","cur"}。
static bool Stream_PomList(uint16_t index, JsonDocument &out)
{
    if (index >= PrescriptConst::MAX_POMODORO_PRESETS)
        return false;
    const auto &preset = sysConfig.pomodoro_presets[index];
    out["slot"] = index;
    out["n"] = preset.name;
    out["w"] = preset.work_min;
    out["r"] = preset.rest_min;
    out["rd"] = preset.rounds;
    out["cur"] = sysConfig.pomodoro_current_idx;
    return true;
}

static const SysCmdSpec kPomCmds[] = {
    {"LIST", nullptr, 0, CMD_SYNC, nullptr, Stream_PomList},
    {"SET", kPomSetArgs, SYS_COUNT_OF(kPomSetArgs), 0, Cmd_PomSet, nullptr},
};

static const SysCmdGroup kPomGroup = {"POM", 1, SRC_ALL, kPomCmds, SYS_COUNT_OF(kPomCmds)};

static void Pomodoro_RegisterCommands()
{
    SysCommand_RegisterGroup(&kPomGroup);
}

// ===== 【蓝牙命令区结束】 =====
