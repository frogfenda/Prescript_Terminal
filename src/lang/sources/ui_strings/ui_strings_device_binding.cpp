/*
【模块职责】device_binding 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *DeviceBindingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "进入绑定模式" : "ENTER BINDING MODE";
    }

    const char *DeviceBindingPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "按主键开启绑定模式" : "CLICK TO ENTER BINDING MODE";
    }

    const char *DeviceBindingRunning(SystemLang_t lang)
    {
        return IsZh(lang) ? "绑定模式已开启" : "BINDING MODE ACTIVE";
    }

    const char *DeviceBindingReadyHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按开启 / 长按退出" : "CLICK ENABLE / HOLD EXIT";
    }

    const char *DeviceBindingWaitingHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "请在 App 中绑定设备 / 长按退出" : "BIND IN APP / HOLD EXIT";
    }

    const char *BlePairingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "进入蓝牙配对" : "ENTER BLE PAIRING";
    }

    const char *BlePairingPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按清除旧手机并配对" : "CLICK TO REPLACE OLD PHONE";
    }

    const char *BlePairingRunning(SystemLang_t lang)
    {
        return IsZh(lang) ? "蓝牙配对模式已开启" : "BLE PAIRING ACTIVE";
    }

    const char *BlePairingReadyHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按开启 / 长按退出" : "CLICK ENABLE / HOLD EXIT";
    }

    const char *BlePairingWaitingHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "请在手机中完成配对 / 长按退出" : "PAIR ON PHONE / HOLD EXIT";
    }

    // 【传感器校准测试】集中承载动作、人体坐标与地磁维护入口，避免系统设置一级菜单过长。
}
