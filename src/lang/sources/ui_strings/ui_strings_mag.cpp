/*
【模块职责】mag 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *MagDiagnosticsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "地磁诊断" : "MAG DIAGNOSTICS";
    }

    const char *MagUnavailable(SystemLang_t lang)
    {
        return IsZh(lang) ? "MMC5603NJ不可用，正在低频重试" : "MMC5603NJ OFFLINE - RETRYING";
    }

    const char *MagLiveHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "旋钮换页 / 串口每0.5秒输出 / 长按退出" : "TURN PAGE / SERIAL 0.5S / HOLD EXIT";
    }

    const char *MagCalibrationReadyHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "主键开始三维校准 / 长按退出" : "CLICK TO START 3D CAL / HOLD EXIT";
    }

    const char *MagCalibrationRunningHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "缓慢覆盖所有方向 / 主键结束拟合" : "ROTATE ALL DIRECTIONS / CLICK FINISH";
    }

    const char *MagCalibrationCancelHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "侧键取消本轮采集" : "SIDE CLICK CANCELS";
    }

    // 【业力】沉浸式木鱼页面的模式、累计次数和清空确认文本；三个模式名称暂时相同。
}
