/*
【模块职责】alarm 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char **AlarmStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定小时", "设定分钟"};
        static const char *en_items[] = {"SET HR", "SET MIN"};
        return IsZh(lang) ? zh_items : en_items;
    }

    // 删除已移到二级菜单；编辑已有闹钟时第一步长按只返回二级菜单。
    const char *AlarmEditFirstTip(SystemLang_t lang, bool editingExisting)
    {
        if (IsZh(lang))
            return editingExisting ? "长按返回 / 单击下一步" : "长按取消新建 / 单击确认";
        return editingExisting ? "LONG: BACK / CLICK: NEXT" : "LONG: CANCEL / CLICK: NEXT";
    }

    // 【闹钟二级菜单】状态前缀与 AlarmActiveState 拼成“状态：开”。
    const char *AlarmItemStatePrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "状态：" : "STATE: ";
    }

    const char *AlarmItemSetTime(SystemLang_t lang)
    {
        return IsZh(lang) ? "更改时间" : "SET TIME";
    }

    const char *AlarmItemDelete(SystemLang_t lang)
    {
        return IsZh(lang) ? "删除此闹钟" : "DELETE ALARM";
    }

    // 闹钟/日程二级菜单共用的删除确认正文格式，%s 为闹钟名或日程标题。
    const char *AlarmEditSaveTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";
    }

    const char *AlarmTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市唤醒闹钟" : "WAKEUP ALARM";
    }

    const char *AlarmAddItem(SystemLang_t lang)
    {
        return IsZh(lang) ? " + 添加新闹钟" : " + ADD NEW ALARM";
    }

    const char *AlarmActiveState(SystemLang_t lang, bool active)
    {
        if (IsZh(lang))
            return active ? "开" : "关";
        return active ? "ON" : "OFF";
    }

    // 【番茄钟】运行态、预设选择和当前预设编辑文本。
}
