/*
【模块职责】human_frame 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *HumanFrameDriftTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "坐标漂移测试" : "FRAME DRIFT TEST";
    }

    const char *HumanFrameCalibratingHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "静止约1秒 / 长按退出" : "STILL 1S / HOLD EXIT";
    }

    const char *HumanFrameDiscontinuous(SystemLang_t lang)
    {
        return IsZh(lang) ? "采样已断开，追踪冻结" : "SAMPLE GAP - TRACKING FROZEN";
    }

    const char *HumanFrameDiscontinuousDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "不能安全续接当前姿态" : "CURRENT POSE CANNOT RESUME";
    }

    const char *HumanFrameAttitudePage(SystemLang_t lang)
    {
        return IsZh(lang) ? "姿态" : "ATTITUDE";
    }

    const char *HumanFrameAccelerationPage(SystemLang_t lang)
    {
        return IsZh(lang) ? "绝对人体坐标去重力加速度" : "ABS HUMAN LINEAR ACCEL";
    }

    const char *HumanFrameStatisticsPage(SystemLang_t lang)
    {
        return IsZh(lang) ? "统计" : "STATS";
    }

    const char *HumanFrameRunningHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "旋钮换页 / 主键暂停 / 长按退出" : "TURN PAGE / CLICK PAUSE / HOLD EXIT";
    }

    const char *HumanFramePausedHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "显示暂停，积分继续 / 主键恢复" : "DISPLAY PAUSED, TRACKING CONTINUES";
    }

    const char *HumanFrameRecalibrateHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "连续短按两次侧键重新校准" : "SIDE CLICK TWICE TO RECALIBRATE";
    }

    const char *HumanFrameRecalibrateConfirm(SystemLang_t lang)
    {
        return IsZh(lang) ? "再按一次侧键清空并重新校准" : "SIDE CLICK AGAIN TO RESET";
    }

    // 【地磁诊断】独立磁场服务的实时数据、三维校准和操作提示。
}
