/*
【模块职责】push 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *PushSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "指令推送配置" : "PUSH CONFIG";
    }

    const char *PushEnableLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "指令推送: " : "AUTO PUSH: ";
    }

    const char *PushMinLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "最短潜伏: " : "MIN TIME: ";
    }

    const char *PushMaxLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "最长潜伏: " : "MAX TIME: ";
    }

    const char *PushMinuteSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? " 分钟" : " MIN";
    }

    const char *PushUserItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "使用者" : "USER";
    }
}
