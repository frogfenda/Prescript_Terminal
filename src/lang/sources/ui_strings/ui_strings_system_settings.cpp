/*
【模块职责】system_settings 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *LanguageBuildItem(SystemLang_t uiLang, SystemLang_t buildLang)
    {
        if (TerminalLang::LOCKED)
        {
            if (buildLang == LANG_ZH)
                return IsZh(uiLang) ? "语言版本: 中文" : "LANG: ZH BUILD";
            return IsZh(uiLang) ? "语言版本: 英文" : "LANG: EN BUILD";
        }
        return IsZh(uiLang) ? "切换系统语言" : "SWITCH LANGUAGE";
    }

    const char *LanguageLockedTip(SystemLang_t uiLang)
    {
        return IsZh(uiLang) ? "当前固件语言已固定" : "LANGUAGE LOCKED BY BUILD";
    }

    const char *SystemSettingsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }

    const char *SystemSettingsItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "",
            "时间设置",
            "提取部统计",
            "",
            "设定休眠时间",
            "音量与振动",
            "解码动画配置",
            "传感器校准测试",
            "进入绑定模式",
            "返回上一级"};
        static const char *en_items[] = {
            "",
            "TIME CONFIG",
            "GACHA STATS",
            "",
            "SLEEP SETTINGS",
            "VOL&HAPTIC",
            "ANIMATION SETUP",
            "SENSOR CAL & TEST",
            "ENTER BINDING MODE",
            "BACK TO MAIN"};

        if (index < 0 || index >= 10)
            return "";
        if (index == 3)
            return LanguageBuildItem(lang, TerminalLang::DEFAULT_LANG);
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }
}
