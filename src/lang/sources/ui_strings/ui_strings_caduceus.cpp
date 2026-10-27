/*
【模块职责】caduceus 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *CaduceusTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "双蛇杖" : "CADUCEUS";
    }

    const char *CaduceusActionTestTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "动作测试" : "ACTION TEST";
    }

    const char *CaduceusResourceLoading(SystemLang_t lang)
    {
        return IsZh(lang) ? "正在加载武器资源" : "LOADING WEAPON ASSETS";
    }

    const char *CaduceusResourceLoadingDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "正在准备音频与图片" : "PREPARING AUDIO AND IMAGES";
    }

    const char *CaduceusResourceUnavailable(SystemLang_t lang)
    {
        return IsZh(lang) ? "武器资源暂不可用" : "WEAPON ASSETS UNAVAILABLE";
    }

    const char *CaduceusResourceUnavailableDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "检查资源后重启重试" : "CHECK ASSETS AND RESTART";
    }

    const char *CaduceusWaiting(SystemLang_t lang)
    {
        return IsZh(lang) ? "等待动作..." : "WAITING FOR ACTION...";
    }

    const char *CaduceusCalibrationPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "请将屏幕向上平放" : "PLACE SCREEN UP";
    }

    const char *CaduceusCalibrationDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "保持静止，底边朝向自己" : "KEEP STILL, BOTTOM TOWARD YOU";
    }

    const char *CaduceusStartPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "开始吧" : "BEGIN";
    }

    const char *CaduceusMissingImage(SystemLang_t lang)
    {
        return IsZh(lang) ? "武器图片缺失" : "WEAPON IMAGE MISSING";
    }

    const char *CaduceusActionPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "动作: " : "ACTION: ";
    }

    const char *CaduceusActionName(SystemLang_t lang, int index)
    {
        static const char *zh_actions[] = {"横斩", "竖斩", "斜斩 A", "斜斩 B", "突刺", "上挑"};
        static const char *en_actions[] = {
            "HORIZONTAL", "VERTICAL", "DIAGONAL A", "DIAGONAL B", "THRUST", "UPPERCUT"};
        if (index < 0 || index >= 6)
            return "";
        return IsZh(lang) ? zh_actions[index] : en_actions[index];
    }

    const char *CaduceusDirectionPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "横斩方向: " : "HORIZONTAL DIR: ";
    }

    const char *CaduceusDirectionValue(SystemLang_t lang, int direction)
    {
        if (direction == 0)
            return "--";
        if (IsZh(lang))
            return direction > 0 ? "正向" : "反向";
        return direction > 0 ? "POSITIVE" : "NEGATIVE";
    }

    const char *CaduceusStrengthPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "角速度峰值: " : "GYRO PEAK: ";
    }

    // 【人体坐标测试】入口对齐、正式姿态/加速度观察和必要操作状态的双语文本。
}
