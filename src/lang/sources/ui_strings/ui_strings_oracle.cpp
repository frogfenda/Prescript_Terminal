/*
【模块职责】oracle 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *OracleQuestion(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "你在想什么，代行者？",
            "纺织机已经听见你的迟疑。",
            "把问题交给线与齿轮。",
            "不要说出口，都市会听见。",
            "你的想法正在被编织。",
            "答案尚未抵达，但线已经绷紧。"};
        static const char *en_items[] = {
            "What are you thinking, proxy?",
            "The loom has heard your hesitation.",
            "Give the question to thread and gear.",
            "Do not speak it. The City listens.",
            "Your thought is being woven."};
        int count = IsZh(lang) ? 6 : 5;
        if (index < 0 || index >= count)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    int OracleQuestionCount(SystemLang_t lang)
    {
        return IsZh(lang) ? 6 : 5;
    }

    const char *OracleOption(SystemLang_t lang, int index)
    {
        if (index == 0)
            return IsZh(lang) ? "获取纺织机回复" : "ASK THE LOOM";
        return IsZh(lang) ? "吃什么？" : "WHAT TO EAT?";
    }

    // 【硬币】硬币主菜单、参数设置和预设编辑器。技能名称等用户数据不放在这里。
}
