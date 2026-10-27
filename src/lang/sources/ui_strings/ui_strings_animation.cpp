/*
【模块职责】animation 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *AnimSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "解码动画配置" : "ANIMATION SETUP";
    }

    const char *AnimSettingItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "默认矩阵瀑布",
            "战术游标推进",
            "逐字乱码扫描",
            "全局波浪解码"};
        static const char *en_items[] = {
            "MATRIX FALL",
            "CURSOR TYPEWRITER",
            "SEQUENTIAL GLITCH",
            "GLOBAL WAVE"};

        if (index < 0 || index >= 4)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【音量与振动】左右滑条标题和震动档位。中文界面避免显示 OFF/MIN/MID/MAX。
}
