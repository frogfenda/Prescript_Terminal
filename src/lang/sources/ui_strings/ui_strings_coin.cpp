/*
【模块职责】coin 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *CoinSettingsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "硬币设置" : "FLIP SETTINGS";
    }

    const char *CoinModeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "运行模式" : "MODE:";
    }

    const char *CoinSanityLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "理智值" : "SANITY:";
    }

    const char *CoinCountLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "硬币数量" : "COINS:";
    }

    const char *CoinTypeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "硬币外观" : "STYLE:";
    }

    const char *CoinModeValue(SystemLang_t lang, int mode)
    {
        if (mode == 0)
            return IsZh(lang) ? "自动" : "AUTO";
        return IsZh(lang) ? "手动" : "MANUAL";
    }

    const char *CoinMaterial(SystemLang_t lang, int type)
    {
        if (type == 1)
            return IsZh(lang) ? "红色" : "RED";
        if (type == 2)
            return IsZh(lang) ? "绿色" : "GREEN";
        return IsZh(lang) ? "金色" : "GOLD";
    }

    const char **CoinPresetEditStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"基础威力", "硬币威力", "硬币数量", "硬币外观"};
        static const char *en_items[] = {"BASE", "COIN", "COUNT", "STYLE"};
        return IsZh(lang) ? zh_items : en_items;
    }

    const char *CoinOverrideTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "保存技能预设" : "SAVE PRESET";
    }

    const char *CoinOverrideFormat(SystemLang_t lang)
    {
        return IsZh(lang) ? "覆写 [%s]?" : "OVERRIDE [%s]?";
    }

    const char *CoinSaveAsNew(SystemLang_t lang)
    {
        return IsZh(lang) ? "录入为新技能?" : "SAVE AS NEW?";
    }

    const char *CoinPresetConfirmHint(SystemLang_t lang, bool editingExisting)
    {
        if (editingExisting)
            return IsZh(lang) ? "长按删除 / 单击确认" : "LONG: DELETE / CLICK: CONFIRM";
        return IsZh(lang) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: CONFIRM";
    }

    const char *CoinPresetEditTip(SystemLang_t lang, bool firstNewPhase)
    {
        if (firstNewPhase)
            return IsZh(lang) ? "长按取消 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
        return IsZh(lang) ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
    }

    const char *CoinAutoPresetFormat(SystemLang_t lang)
    {
        return IsZh(lang) ? "预设-%d" : "PRESET-%d";
    }

    const char *CoinMenuTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "硬币决定器" : "COIN FLIP";
    }

    const char *CoinQuickItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 快速抛掷 ]" : "[ QUICK ROLL ]";
    }

    const char *CoinNewPresetItem(SystemLang_t lang)
    {
        return IsZh(lang) ? " + 新建技能预设" : " + NEW PRESET";
    }

    const char *CoinAdvancedItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "硬币设置" : "FLIP SETTINGS";
    }

    // 【日程】日程编辑器、预设标题、过期列表和主列表。真实日程标题/正文仍属于用户数据。
}
