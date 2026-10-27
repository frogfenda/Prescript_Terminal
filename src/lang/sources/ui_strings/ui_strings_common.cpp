/*
【模块职责】common 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    bool IsZh(SystemLang_t lang)
    {
        return lang == LANG_ZH;
    }

    const char *OnOff(SystemLang_t lang, bool enabled)
    {
        if (IsZh(lang))
            return enabled ? "开启" : "关闭";
        return enabled ? "ON" : "OFF";
    }

    const char *BackItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回上一级" : "BACK";
    }

    // 【主菜单】顶层应用入口名称。主菜单项数量必须和 app_main_menu.cpp 的路由数量保持一致。
    const char *SensorCalibrationTestTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "传感器校准测试" : "SENSOR CAL & TEST";
    }

    const char *SensorCalibrationTestItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "动作测试",
            "坐标漂移测试",
            "地磁校准",
            "返回系统设置"};
        static const char *en_items[] = {
            "ACTION TEST",
            "FRAME DRIFT TEST",
            "MAG CALIBRATION",
            "BACK TO SETTINGS"};

        if (index < 0 || index >= 4)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【指令推送配置】自动推送开关、推送间隔和进入使用者页的菜单文本。
    const char *SaveAndReturnItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "保存并返回" : "SAVE & RETURN";
    }

    // 【使用者】本地 ID 列表页和删除确认弹窗。ID 只用于本地显示，不对应网络账号。
    const char *DeleteRecordPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "删除 [%s] ?" : "DEL [%s] ?";
    }

    const char *BackToMainMenuItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回主菜单" : "BACK TO MAIN MENU";
    }

    const char *KeepCurrentOption(SystemLang_t lang)
    {
        return IsZh(lang) ? "保持当前" : "KEEP";
    }
}
