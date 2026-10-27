/*
【模块职责】gacha 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *GachaStatsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "提取部数据库" : "EXTRACTION DATABASE";
    }

    const char *GachaTotalLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "总计提取" : "TOTAL PULLS";
    }

    const char *GachaTotalUnit(SystemLang_t lang)
    {
        return IsZh(lang) ? "次" : "";
    }

    const char *GachaWalpurgisLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "[W] 瓦夜总计" : "[W] WALPURGIS";
    }

    const char *GachaClearItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 按下清除所有记录 ]" : "[ PRESS TO CLEAR RECORDS ]";
    }

    // 【提取部模拟】十连提取页面的固定提示。人格名和结果列表来自资源池和抽取结果。
    const char *GachaIdleHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 单击 ] 执行十连提取" : "[ CLICK ] 10X EXTRACT";
    }

    const char *GachaExtractingLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "提取中..." : "EXTRACTION...";
    }

    // 【网络连接】手动 WiFi 连接页的居中状态文本。
}
