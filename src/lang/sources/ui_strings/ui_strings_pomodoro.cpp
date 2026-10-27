/*
【模块职责】pomodoro 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *PomodoroPhaseName(SystemLang_t lang, int phase)
    {
        return IsZh(lang) ? (phase==0 ? "专注" : "休息") : (phase==0 ? "FOCUS" : "REST");
    }
    const char **PomodoroRunSteps(SystemLang_t lang)
    {
        static const char *zh_items[]={"专注","休息"};
        static const char *en_items[]={"FOCUS","REST"};
        return IsZh(lang) ? zh_items : en_items;
    }
    const char *PomodoroRunningTip(SystemLang_t lang, int phase)
    {
        if (IsZh(lang)) return phase==0 ? "长按退出(后台运行) / 单击休息" : "长按退出(后台运行) / 单击结束";
        return phase==0 ? "LONG: EXIT(BG) / CLICK: REST" : "LONG: EXIT(BG) / CLICK: END";
    }
    const char *PomodoroFinishTitle(SystemLang_t lang, int phase)
    {
        if (IsZh(lang)) return phase==0 ? "结束专注并开始休息？" : "提前结束本轮休息？";
        return phase==0 ? "END FOCUS AND START REST?" : "END THIS REST EARLY?";
    }
    const char *PomodoroFinishHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: CONFIRM";
    }

    const char *PomodoroWorkDonePrescript(SystemLang_t lang)
    {
        return IsZh(lang) ? "专注阶段结束。休息计时已开始。" : "FOCUS COMPLETE. REST TIMER STARTED.";
    }

    const char *PomodoroRestDonePrescript(SystemLang_t lang)
    {
        return IsZh(lang) ? "休息阶段结束。本轮已完成。" : "REST COMPLETE. ROUND FINISHED.";
    }

    const char **PomodoroEditStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"专注时长", "休息时长", "轮次数目"};
        static const char *en_items[] = {"FOCUS MIN", "REST MIN", "ROUNDS"};
        return IsZh(lang) ? zh_items : en_items;
    }

    const char *PomodoroEditTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: OK";
    }

    const char *PomodoroWaitingTip(SystemLang_t lang, bool completed)
    {
        if (IsZh(lang)) return completed ? "本组已完成 / 单击重新开始" : "休息结束 / 单击下一轮 / 长按结束专注";
        return completed ? "SET COMPLETE / CLICK TO RESTART" : "REST DONE / CLICK NEXT / LONG END";
    }

    const char *PomodoroWaitingLabel(SystemLang_t lang, bool completed)
    {
        if (IsZh(lang)) return completed ? "本组完成" : "等待下一轮";
        return completed ? "SET COMPLETE" : "NEXT ROUND";
    }

    const char *PomodoroRoundLabel(SystemLang_t lang)
    { return IsZh(lang) ? "轮次" : "ROUND"; }

    const char *PomodoroStatsTitle(SystemLang_t lang)
    { return IsZh(lang) ? "专注统计" : "FOCUS STATISTICS"; }

    const char *PomodoroStatsClear(SystemLang_t lang)
    { return IsZh(lang) ? "清空当前统计" : "CLEAR CURRENT STATS"; }

    const char *PomodoroCompletedLabel(SystemLang_t lang)
    { return IsZh(lang) ? "完成轮数" : "COMPLETED ROUNDS"; }

    const char *PomodoroFocusTimeLabel(SystemLang_t lang)
    { return IsZh(lang) ? "总专注时长" : "TOTAL FOCUS TIME"; }

    const char *PomodoroStatsClearTitle(SystemLang_t lang)
    { return IsZh(lang) ? "清空当前预设统计？" : "CLEAR CURRENT PRESET STATS?"; }

    const char *PomodoroStatsClearHint(SystemLang_t lang)
    { return IsZh(lang) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: CONFIRM"; }

    const char *PomodoroPresetTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "选择预设配置" : "SELECT PRESET";
    }

    const char *PomodoroTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "番茄专注协议" : "POMODORO";
    }

    const char *PomodoroMenuItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"", "选择系统预设库", "修改当前预设", "显示统计信息"};
        static const char *en_items[] = {"", "SELECT PRESET", "EDIT CURRENT PRESET", "SHOW STATISTICS"};
        if (index < 0 || index >= 4)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    const char *PomodoroRunLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "执行专注" : "RUN";
    }
    const char *PomodoroResumeLabel(SystemLang_t lang, int phase)
    {
        return IsZh(lang) ? (phase==0 ? "继续专注" : "继续休息") : (phase==0 ? "RESUME FOCUS" : "RESUME REST");
    }

    // 【提取部统计】抽卡统计页的标题、统计项和清空提示。
}
