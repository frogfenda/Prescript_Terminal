/*
【模块职责】main_menu 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *MainMenuTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市主控菜单" : "MAIN MENU";
    }

    const char *MainMenuItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "接受指令",
            "纺织机",
            "定时指令",
            "但丁",
            "TT2协议",
            "专注协议",
            "硬币决定器",
            "双蛇杖",
            "提取部模拟",
            "指令档案",
            "指令推送配置",
            "使用者",
            "海",
            "业力",
            "系统高级设置",
            "进入待机模式"};
        static const char *en_items[] = {
            "RECEIVE PRESCRIPT",
            "LOOM",
            "SCHEDULES",
            "WAKEUP ALARM",
            "TT2 PROTOCOL",
            "POMODORO TIMER",
            "QUANTUM COIN",
            "CADUCEUS",
            "EXTRACTION SIM",
            "PRESCRIPT DB",
            "PUSH SETTINGS",
            "USER",
            "SEA",
            "KARMA",
            "SYSTEM SETTINGS",
            "STANDBY MODE"};

        if (index < 0 || index >= 16)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【双蛇杖】运行时标定骨架的固定文本。动作名称下标由 App 内的语义映射产生，不依赖枚举数值。
}
