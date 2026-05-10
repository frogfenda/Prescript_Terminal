#pragma once
#include "terminal_lang.h"

// Early shared UI labels. Stage 6A only moves labels that are touched by
// language-edition behavior; full text migration can continue module by module.
namespace UIStrings
{
    inline const char *SystemSettingsTitle(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "系统设置菜单" : "SYSTEM SETTINGS";
    }

    inline const char *LanguageBuildItem(SystemLang_t uiLang, SystemLang_t buildLang)
    {
        if (TerminalLang::LOCKED)
        {
            if (buildLang == LANG_ZH)
                return uiLang == LANG_ZH ? "语言版本: 中文" : "LANG: ZH BUILD";
            return uiLang == LANG_ZH ? "语言版本: 英文" : "LANG: EN BUILD";
        }
        return uiLang == LANG_ZH ? "切换系统语言" : "SWITCH LANGUAGE";
    }

    inline const char *LanguageLockedTip(SystemLang_t uiLang)
    {
        return uiLang == LANG_ZH ? "当前固件语言已固定" : "LANGUAGE LOCKED BY BUILD";
    }
}
