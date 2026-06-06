#pragma once
#include "terminal_lang.h"

// 【模块职责】集中管理短 UI 文本。
// 大段业务内容继续放在 data/zh、data/en 资源文件里；这里主要收纳菜单、按钮、提示这类高频短字符串。
namespace UIStrings
{
    inline bool IsZh(SystemLang_t lang)
    {
        return lang == LANG_ZH;
    }

    inline const char *OnOff(SystemLang_t lang, bool enabled)
    {
        if (IsZh(lang))
            return enabled ? "开启" : "关闭";
        return enabled ? "ON" : "OFF";
    }

    inline const char *BackItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回上一级" : "BACK";
    }

    inline const char *MainMenuTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市主控菜单" : "MAIN MENU";
    }

    inline const char *MainMenuItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "接受指令",
            "纺织机",
            "定时指令",
            "但丁",
            "TT2协议",
            "专注协议",
            "硬币决定器",
            "提取部模拟",
            "指令档案",
            "指令推送配置",
            "使用者",
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
            "EXTRACTION SIM",
            "PRESCRIPT DB",
            "PUSH SETTINGS",
            "USER",
            "SYSTEM SETTINGS",
            "STANDBY MODE"};

        if (index < 0 || index >= 13)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char *LanguageBuildItem(SystemLang_t uiLang, SystemLang_t buildLang)
    {
        if (TerminalLang::LOCKED)
        {
            if (buildLang == LANG_ZH)
                return IsZh(uiLang) ? "语言版本: 中文" : "LANG: ZH BUILD";
            return IsZh(uiLang) ? "语言版本: 英文" : "LANG: EN BUILD";
        }
        return IsZh(uiLang) ? "切换系统语言" : "SWITCH LANGUAGE";
    }

    inline const char *LanguageLockedTip(SystemLang_t uiLang)
    {
        return IsZh(uiLang) ? "当前固件语言已固定" : "LANGUAGE LOCKED BY BUILD";
    }

    inline const char *SystemSettingsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }

    inline const char *WifiDisconnectItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "断开无线网络" : "DISCONNECT WIFI";
    }

    inline const char *WifiBusyItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络运行中..." : "WIFI BUSY...";
    }

    inline const char *WifiConnectItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "连接无线网络" : "CONNECT WIFI";
    }

    inline const char *SystemSettingsItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "",
            "同步网络时间",
            "时间设置",
            "提取部统计",
            "",
            "设定休眠时间",
            "音量与振动",
            "解码动画配置",
            "返回上一级"};
        static const char *en_items[] = {
            "",
            "SYNC NTP TIME",
            "TIME CONFIG",
            "GACHA STATS",
            "",
            "SLEEP SETTINGS",
            "VOL&HAPTIC",
            "ANIMATION SETUP",
            "BACK TO MAIN"};

        if (index < 0 || index >= 9)
            return "";
        if (index == 4)
            return LanguageBuildItem(lang, TerminalLang::DEFAULT_LANG);
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char *PushSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "指令推送配置" : "PUSH CONFIG";
    }

    inline const char *PushEnableLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "指令推送: " : "AUTO PUSH: ";
    }

    inline const char *PushMinLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "最短潜伏: " : "MIN TIME: ";
    }

    inline const char *PushMaxLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "最长潜伏: " : "MAX TIME: ";
    }

    inline const char *PushMinuteSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? " 分钟" : " MIN";
    }

    inline const char *PushUserItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "使用者" : "USER";
    }

    inline const char *SaveAndReturnItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "保存并接入主系统" : "SAVE & RETURN";
    }

    inline const char *UserTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "使用者" : "USER";
    }

    inline const char *ClearCurrentUserItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "清空当前使用者" : "CLEAR CURRENT USER";
    }

    inline const char *CurrentUserSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? " < 当前" : " < CURRENT";
    }

    inline const char *DangerTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "危险操作" : "DANGER";
    }

    inline const char *DeleteUserHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按删除 / 长按取消" : "CLICK DELETE / HOLD CANCEL";
    }

    inline const char *ArchiveDeleteTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "确认删除此指令？" : "DELETE THIS RECORD?";
    }

    inline const char *ArchiveEmpty(SystemLang_t lang)
    {
        return IsZh(lang) ? "数据库为空" : "DB ARCHIVE EMPTY";
    }

    inline const char *ClickBackHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按返回" : "CLICK BACK";
    }

    inline const char *HoldBackHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按返回" : "HOLD BACK";
    }

    inline const char *ClickDeleteHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按删除" : "CLICK DELETE";
    }

    inline const char *HoldExitHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按退出" : "HOLD EXIT";
    }

    inline const char *PurgingRecord(SystemLang_t lang)
    {
        return IsZh(lang) ? "正在删除指令..." : "PURGING RECORD...";
    }
}
