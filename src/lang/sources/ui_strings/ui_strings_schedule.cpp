/*
【模块职责】schedule 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *ScheduleTitlePreset(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
        static const char *en_items[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
        if (index < 0 || index >= 4)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    const char *ScheduleTextPreset(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"", "日程时间已到，请立即执行。"};
        static const char *en_items[] = {"", "SCHEDULE TIME REACHED. EXECUTE NOW."};
        if (index < 0 || index >= 2)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    const char **ScheduleEditStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定月份", "设定日期", "设定小时", "设定分钟", "选择类型", "执行动作"};
        static const char *en_items[] = {"SET MON", "SET DAY", "SET HR", "SET MIN", "SCH TYPE", "ACTION"};
        return IsZh(lang) ? zh_items : en_items;
    }

    const char *ScheduleTypeName(SystemLang_t lang, int index, bool compact)
    {
        static const char *zh_items[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
        static const char *en_full[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
        static const char *en_compact[] = {"ROUTINE", "MEETING", "MAINTAIN", "EMERGENCY"};
        if (index < 0 || index >= 4)
            index = 0;
        if (IsZh(lang))
            return zh_items[index];
        return compact ? en_compact[index] : en_full[index];
    }

    const char *ScheduleActionName(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"随机指令", "固定提醒"};
        static const char *en_items[] = {"RANDOM", "FIXED MSG"};
        if (index < 0 || index >= 2)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    /**
     * 日程编辑器底部提示。
     * first/last 指当前编辑模式内的第一步和最后一步（改时间模式只有月日时分，改内容模式只有类型和动作）；
     * context：0 新建，1 编辑已有日程，2 从过期库恢复。第一步长按分别是取消新建、返回二级菜单、取消恢复。
     */
    const char *ScheduleEditTip(SystemLang_t lang, bool first, bool last, int context)
    {
        const bool zh = IsZh(lang);
        if (last)
            return zh ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";
        if (first && context == 0)
            return zh ? "长按取消新建 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
        if (first && context == 2)
            return zh ? "长按取消恢复 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
        return zh ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
    }

    // 【日程二级菜单】时间与内容分开修改；删除确认复用 DeleteRecordPrompt / DeleteUserHint。
    const char *ScheduleItemSetTime(SystemLang_t lang)
    {
        return IsZh(lang) ? "重新配置时间" : "SET TIME";
    }

    const char *ScheduleItemSetContent(SystemLang_t lang)
    {
        return IsZh(lang) ? "更改标题与提示" : "TITLE & ACTION";
    }

    const char *ScheduleItemDelete(SystemLang_t lang)
    {
        return IsZh(lang) ? "删除此日程" : "DELETE SCHEDULE";
    }

    // 类型/动作滚轮的首项：当前值不是任何预设（例如手机写入的自定义标题）时出现，保存时原样保留。
    const char *ScheduleExpiredTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "已过期日程收容所" : "EXPIRED ARCHIVE";
    }

    const char *ScheduleBackToList(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回上一级" : "BACK TO SCH";
    }

    const char *ScheduleExpiredMark(SystemLang_t lang)
    {
        return IsZh(lang) ? "(过期)" : "(EXP)";
    }

    const char *ScheduleTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市日程计划" : "SCHEDULES";
    }

    const char *ScheduleOpenExpiredItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 打开已过期日程库 ]" : "[ EXPIRED ARCHIVE ]";
    }

    const char *ScheduleAddItem(SystemLang_t lang)
    {
        return IsZh(lang) ? " + 登记新日程" : " + ADD SCHEDULE";
    }
}
