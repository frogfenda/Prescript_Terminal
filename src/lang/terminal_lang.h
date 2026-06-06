#pragma once
#include <Arduino.h>

// Compile-time language selection for Prescript Terminal.
//
// Existing builds without TERMINAL_LANG_ZH / TERMINAL_LANG_EN keep legacy
// runtime language switching. Add one of these build flags to lock a firmware:
//   -D TERMINAL_LANG_ZH
//   -D TERMINAL_LANG_EN
//
// This lets the project ship as two language editions while the core code stays shared.

enum SystemLang_t : uint8_t
{
    LANG_EN = 0,
    LANG_ZH = 1
};

#if defined(TERMINAL_LANG_ZH) && defined(TERMINAL_LANG_EN)
#error "Define only one of TERMINAL_LANG_ZH or TERMINAL_LANG_EN."
#endif

namespace TerminalLang
{
#if defined(TERMINAL_LANG_ZH)
    constexpr bool LOCKED = true;
    constexpr SystemLang_t DEFAULT_LANG = LANG_ZH;
    constexpr const char *BUILD_CODE = "ZH";
    constexpr const char *BUILD_LABEL_ZH = "中文版本";
    constexpr const char *BUILD_LABEL_EN = "ZH BUILD";
#elif defined(TERMINAL_LANG_EN)
    constexpr bool LOCKED = true;
    constexpr SystemLang_t DEFAULT_LANG = LANG_EN;
    constexpr const char *BUILD_CODE = "EN";
    constexpr const char *BUILD_LABEL_ZH = "英文版本";
    constexpr const char *BUILD_LABEL_EN = "EN BUILD";
#else
    constexpr bool LOCKED = false;
    constexpr SystemLang_t DEFAULT_LANG = LANG_ZH;
    constexpr const char *BUILD_CODE = "RT";
    constexpr const char *BUILD_LABEL_ZH = "运行时语言";
    constexpr const char *BUILD_LABEL_EN = "RUNTIME LANG";
#endif

    inline SystemLang_t Normalize(uint8_t raw)
    {
        return raw == LANG_EN ? LANG_EN : LANG_ZH;
    }

    inline const char *Code(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "ZH" : "EN";
    }

    inline const char *LowerCode(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "zh" : "en";
    }

    inline const char *DisplayName(SystemLang_t lang, SystemLang_t uiLang)
    {
        if (lang == LANG_ZH)
            return uiLang == LANG_ZH ? "中文" : "CHINESE";
        return uiLang == LANG_ZH ? "英文" : "ENGLISH";
    }

    inline const char *BuildLabel(SystemLang_t uiLang)
    {
        return uiLang == LANG_ZH ? BUILD_LABEL_ZH : BUILD_LABEL_EN;
    }

    inline bool IsLockedTo(SystemLang_t lang)
    {
        return LOCKED && DEFAULT_LANG == lang;
    }

    inline bool Accepts(SystemLang_t lang)
    {
        return !LOCKED || DEFAULT_LANG == lang;
    }

    inline const char *PrescriptPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/zh/prescripts_zh.txt" : "/en/prescripts_en.txt";
    }

    inline const char *LegacyPrescriptPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/assets/prescripts_zh.txt" : "/assets/prescripts_en.txt";
    }

    inline const char *PrescriptFallback(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "系统覆盖。请上传 zh/prescripts_zh.txt" : "SYS OVERRIDE. PLEASE UPLOAD en/prescripts_en.txt.";
    }

    inline const char *SpecialsPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/zh/specials_zh.json" : "/en/specials_en.json";
    }

    inline const char *LegacySpecialsPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/assets/specials_zh.json" : "/assets/specials_en.json";
    }

    inline const char *OraclePath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/zh/oracle_zh.json" : "/en/oracle_en.json";
    }

    inline const char *LegacyOraclePath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/assets/oracle_zh.json" : "/assets/oracle_en.json";
    }

    inline const char *IdsPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/zh/ids_zh.json" : "/en/ids_en.json";
    }

    inline const char *ConfigPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/zh/config.json" : "/en/config.json";
    }

    inline const char *DefaultConfigPath(SystemLang_t lang)
    {
        return lang == LANG_ZH ? "/zh/config_zh.json" : "/en/config_en.json";
    }

    inline const char *LegacyIdsPath()
    {
        return "/assets/ids.json";
    }
}
