/*
【模块职责】time 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char **TimeManualStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定小时", "设定分钟"};
        static const char *en_items[] = {"SET HOUR", "SET MIN"};
        return IsZh(lang) ? zh_items : en_items;
    }

    const char **TimeDateStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定年份", "设定月份", "设定日期"};
        static const char *en_items[] = {"SET YEAR", "SET MONTH", "SET DAY"};
        return IsZh(lang) ? zh_items : en_items;
    }

    const char *TimeEditTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按下一步/保存  长按返回" : "CLICK: NEXT/SAVE  LONG: BACK";
    }

    const char *MonthSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? "月" : "M";
    }

    const char *DaySuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? "日" : "D";
    }

    const char *TimeSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "时间设置" : "TIME CONFIG";
    }

    /** 时间设置一级菜单的动态日期时间前缀；具体 YYYY-MM-DD HH:MM 由 SysTime 提供。 */
    const char *CurrentTimeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "当前时间: " : "CURRENT TIME: ";
    }

    const char *TimeSettingItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "",
            "设置当日时间",
            "日期设置",
            "网络校时"};
        static const char *en_items[] = {
            "",
            "SET TODAY TIME",
            "SET DATE",
            "NETWORK SYNC"};

        if (index < 0 || index >= 4)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【解码动画配置】指令解码动画选择页。
}
