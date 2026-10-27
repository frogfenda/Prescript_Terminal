/*
【模块职责】karma 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *KarmaModePrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "当前模式[" : "MODE [";
    }

    const char *KarmaModeName(SystemLang_t lang, uint8_t mode)
    {
        (void)mode;
        return IsZh(lang) ? "业" : "KARMA";
    }

    const char *KarmaModeSuffix(SystemLang_t lang)
    {
        (void)lang;
        return "]";
    }

    const char *KarmaCountPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "你已经累计了" : "ACCUMULATED ";
    }

    const char *KarmaCountUnit(SystemLang_t lang)
    {
        return IsZh(lang) ? "点" : " ";
    }

    const char *KarmaCountName(SystemLang_t lang)
    {
        return IsZh(lang) ? "业" : "KARMA";
    }

    const char *KarmaClearTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "清空业力？" : "CLEAR KARMA?";
    }

    const char *KarmaClearMessage(SystemLang_t lang)
    {
        return IsZh(lang) ? "仅清空当前模式" : "CURRENT MODE ONLY";
    }

    const char *KarmaClearHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按清空 / 长按取消" : "CLICK CLEAR / HOLD CANCEL";
    }

    const char *KarmaMissingImage(SystemLang_t lang)
    {
        return IsZh(lang) ? "业力图像缺失" : "KARMA IMAGE MISSING";
    }

    // 【系统设置】系统高级设置页的条目、网络状态和语言锁定提示。语言项会根据编译宏显示“切换”或“固定版本”。
}
