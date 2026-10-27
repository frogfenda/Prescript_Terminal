/*
【模块职责】volume 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *VolumeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "音量输出" : "VOLUME";
    }

    const char *HapticLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "触感反馈" : "HAPTIC";
    }

    const char *LevelOff(SystemLang_t lang)
    {
        return IsZh(lang) ? "关闭" : "OFF";
    }

    const char *LevelMin(SystemLang_t lang)
    {
        return IsZh(lang) ? "低" : "MIN";
    }

    const char *LevelMid(SystemLang_t lang)
    {
        return IsZh(lang) ? "中" : "MID";
    }

    const char *LevelMax(SystemLang_t lang)
    {
        return IsZh(lang) ? "高" : "MAX";
    }

    // 【休眠设置】电源策略的多级菜单文本。menuLevel: 0 主菜单，1 待机，2 熄屏休眠。
}
