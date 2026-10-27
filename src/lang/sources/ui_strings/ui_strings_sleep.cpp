/*
【模块职责】sleep 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *SleepSettingTitle(SystemLang_t lang, int menuLevel)
    {
        if (IsZh(lang))
        {
            if (menuLevel == 0) return "电源与休眠策略";
            if (menuLevel == 1) return "待机触发 (亮屏展示)";
            if (menuLevel == 2) return "深度休眠 (物理黑屏)";
        }
        else
        {
            if (menuLevel == 0) return "POWER POLICY";
            if (menuLevel == 1) return "STANDBY (SCREEN ON)";
            if (menuLevel == 2) return "DEEP SLEEP (SCREEN OFF)";
        }
        return "";
    }

    const char *SleepSettingItem(SystemLang_t lang, int menuLevel, int index)
    {
        static const char *zh_main[] = {"设定待机时间", "设定深度休眠"};
        static const char *en_main[] = {"STANDBY TIMEOUT", "SLEEP TIMEOUT"};
        static const char *zh_standby[] = {"30 秒", "60 秒", "5 分钟", "永不待机"};
        static const char *en_standby[] = {"30 SEC", "60 SEC", "5 MIN", "NEVER"};
        static const char *zh_sleep[] = {"3 秒 (立刻)", "30 秒", "60 秒", "永不休眠"};
        static const char *en_sleep[] = {"3 SEC (INSTANT)", "30 SEC", "60 SEC", "NEVER"};

        if (menuLevel == 0 && index >= 0 && index < 2)
            return IsZh(lang) ? zh_main[index] : en_main[index];
        if (menuLevel == 1 && index >= 0 && index < 4)
            return IsZh(lang) ? zh_standby[index] : en_standby[index];
        if (menuLevel == 2 && index >= 0 && index < 4)
            return IsZh(lang) ? zh_sleep[index] : en_sleep[index];
        return "";
    }

    // 【闹钟】闹钟列表、小时/分钟编辑器和删除确认文本。
}
