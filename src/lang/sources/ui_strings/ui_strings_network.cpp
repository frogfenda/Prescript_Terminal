/*
【模块职责】network 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *WifiDisconnectItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "断开无线网络" : "DISCONNECT WIFI";
    }

    const char *WifiBusyItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络运行中..." : "WIFI BUSY...";
    }

    const char *WifiConnectItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "连接无线网络" : "CONNECT WIFI";
    }

    const char *WifiDisconnected(SystemLang_t lang)
    {
        return IsZh(lang) ? "已切断神经网" : "WIFI DISCONNECTED";
    }

    const char *WifiRunning(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络已在后台运行" : "WIFI IS RUNNING";
    }

    const char *WifiInit(SystemLang_t lang)
    {
        return IsZh(lang) ? "启动网络模块" : "INIT NETWORK";
    }

    const char *WifiConnecting(SystemLang_t lang)
    {
        return IsZh(lang) ? "连接神经网" : "CONNECTING";
    }

    const char *WifiConnected(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络已接入!" : "WIFI CONNECTED!";
    }

    const char *WifiError(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络连接异常!" : "NETWORK ERROR!";
    }

    // 【网络同步】完整同步页的两行状态文本，包含连接、NTP、设备信箱、成功和失败状态。
    const char *NetworkSyncPrimary(SystemLang_t lang, int state)
    {
        if (state == 0)
            return IsZh(lang) ? "连接神经网" : "CONNECTING";
        if (state == 1)
            return IsZh(lang) ? "获取网络时间" : "SYNCING NTP";
        if (state == 2)
            return IsZh(lang) ? "同步成功!" : "SYNC SUCCESS!";
        if (state == 3)
            return IsZh(lang) ? "同步失败" : "SYNC FAILED";
        if (state == 4)
            return IsZh(lang) ? "收取设备信件" : "FETCHING MAIL";
        return "";
    }

    const char *NetworkSyncSecondary(SystemLang_t lang, int state)
    {
        if (state == 0)
            return IsZh(lang) ? "请稍候" : "PLEASE WAIT";
        if (state == 1)
            return IsZh(lang) ? "NTP 同步中" : "PLEASE WAIT";
        if (state == 2)
            return IsZh(lang) ? "数据已更新" : "DATA UPDATED";
        if (state == 3)
            return IsZh(lang) ? "请检查网络" : "CHECK WIFI";
        if (state == 4)
            return IsZh(lang) ? "潜入网络中" : "DOWNLOADING";
        return "";
    }

    // 【TT2 倒计时】倒计时链路、危险确认和到点默认指令。动态数字仍由业务层填入。
}
