/*
【模块职责】countdown 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char **CountdownStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定分钟", "设定秒数", "执行中"};
        static const char *en_items[] = {"SET MIN", "SET SEC", "RUNNING"};
        return IsZh(lang) ? zh_items : en_items;
    }

    const char *CountdownRunningLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "执行中" : "RUNNING";
    }

    const char *CountdownAbortTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "确认撤收任务 ?" : "ABORT PROTOCOL ?";
    }

    const char *CountdownAbortHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按取消 / 单击撤收" : "LONG: CANCEL / CLICK: ABORT";
    }

    const char *CountdownSetTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "你想到达多久之后的未来" : "HOW FAR INTO THE FUTURE?";
    }

    const char *CountdownRunningTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按退出(后台运行) / 单击撤收" : "LONG: EXIT(BG) / CLICK: ABORT";
    }

    const char *CountdownDoneFormat(SystemLang_t lang)
    {
        return IsZh(lang) ? "TT2协议结束,你已经到达%d分%d秒后的未来" : "TT2 PROTOCOL ENDED. REACHED %dM %dS IN FUTURE.";
    }

    // 【纺织机】入口问句和两个操作选项。答案内容仍从 oracle_zh/en.json 资源读取。
}
