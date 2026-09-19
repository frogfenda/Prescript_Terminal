/*
【模块职责】时间设置页面组。

本文件定义三个页面：
1. AppTimeSetting：时间设置一级菜单，使用 AppMenuBase 的滚轮菜单；
2. AppTimeManualSet：设置“当日时分”，使用 TacticalLinkEngine + DialAnimator 链路式编辑；
3. AppTimeDateSet：设置“当前日期”，同样使用链路式编辑。

本轮调整点：
- 时间设置一级菜单首项显示当前 YYYY-MM-DD HH:MM，复用非阻塞 SysTime 接口并随菜单分钟刷新更新；
- “设置当日时间”后面新增“日期设置”；
- 当日时间设置通过 SysTime 的统一写入口设置指定时分，不做偏置叠加；
- 时间设置只保留当前时间、手动时分、日期和立即网络校时；周期联网由网络服务固定调度。

交互约定：
- 一级菜单：短按进入，长按返回；
- 链路编辑页：短按下一步/保存，长按上一步/返回；
- 旋钮：菜单选项切换或字段数值调整。
*/
#include "sys/app_base.h"
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "sys/sys_time.h"
#include "sys/sys_audio.h"
#include "ui/ui_frame.h"
#include "lang/ui_strings.h"

/** 把日字段钳制到当前年月的最大天数，防止出现非法日期。 */
static uint8_t _ClampDay(uint16_t year, uint8_t month, uint8_t day)
{
    uint8_t max_day = SysTime_DaysInMonth(year, month);
    if (day < 1) return 1;
    if (day > max_day) return max_day;
    return day;
}

/**
 * 设置当日时分页面。
 *
 * 画面结构：
 * - 顶部链路：设定小时 → 设定分钟；
 * - 中间战术分隔线；
 * - 中部数字滚轮；
 * - 底部操作提示。
 */
class AppTimeManualSet : public AppBase
{
private:
    int hour = 8;
    int minute = 0;
    int phase = 0; // 0=小时，1=分钟

    DialAnimator dialAnim;
    TacticalLinkEngine linkAnim;

    /** 根据当前 phase 绘制小时/分钟编辑界面。 */
    void drawUI()
    {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();

        linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::TimeManualStepNames(lang), 2, phase, 95);

        UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

        if (phase == 0)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), hour, 0, 23, "");
        else
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), minute, 0, 59, "");

        UIFrame::DrawTip(UIStrings::TimeEditTip(lang));
        HAL_Screen_Update();
    }

public:
    /** 进入页面时读取当前本地时分，作为滚轮初始值。 */
    void onCreate() override
    {
        struct tm info;
        SysTime_GetInfo(&info);

        hour = info.tm_hour;
        minute = info.tm_min;
        phase = 0;

        linkAnim.jumpTo(phase);
        drawUI();
    }

    /** 从下级/后台恢复时重绘当前链路页。 */
    void onResume() override { drawUI(); }

    /** 推进数字滚轮和顶部链路动画；只有动画变化时才重绘。 */
    void onLoop() override
    {
        bool dial_changed = dialAnim.update();
        bool link_changed = linkAnim.update(phase);

        if (dial_changed || link_changed)
            drawUI();
    }

    void onDestroy() override {}

    /** 旋钮调整当前字段：小时 0~23 循环，分钟 0~59 循环。 */
    void onKnob(int delta) override
    {
        if (phase == 0)
        {
            hour += delta;
            if (hour < 0) hour = 23;
            if (hour > 23) hour = 0;
        }
        else
        {
            minute += delta;
            if (minute < 0) minute = 59;
            if (minute > 59) minute = 0;
        }

        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    /**
     * 短按推进或保存。
     *
     * 保存时调用 SysTime_SetTodayClock(hour, minute)。该函数会直接设置当天 hour:minute:00，
     * 不会把输入值当作偏置叠加到当前时间上。
     */
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();

        if (phase == 0)
        {
            phase = 1;
            drawUI();
            return;
        }

        SysTime_SetTodayClock((uint8_t)hour, (uint8_t)minute);
        appManager.popApp();
    }

    /** 长按：分钟阶段退回小时阶段；小时阶段退出不保存。 */
    void onKeyLong() override
    {
        SYS_SOUND_NAV();

        if (phase > 0)
        {
            phase--;
            drawUI();
        }
        else
        {
            appManager.popApp();
        }
    }
};

AppTimeManualSet instanceTimeManualSet;
AppBase *appTimeManualSet = &instanceTimeManualSet;

/**
 * 日期设置页面。
 *
 * 画面结构：
 * - 顶部链路：年份 → 月份 → 日期；
 * - 中间战术分隔线；
 * - 中部数字滚轮；
 * - 底部提示。
 *
 * 年份范围暂定 2020~2035，避免用户在小屏上滚动过大范围。
 */
class AppTimeDateSet : public AppBase
{
private:
    static constexpr uint16_t YEAR_MIN = 2020;
    static constexpr uint16_t YEAR_MAX = 2035;

    uint16_t year = 2026;
    uint8_t month = 1;
    uint8_t day = 1;
    int phase = 0; // 0=年，1=月，2=日

    DialAnimator dialAnim;
    TacticalLinkEngine linkAnim;

    /** 当前年月变化后重新限制日期，避免保存非法日期。 */
    void clampDay()
    {
        day = _ClampDay(year, month, day);
    }

    /** 根据当前 phase 绘制年/月/日编辑界面。 */
    void drawUI()
    {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();

        linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::TimeDateStepNames(lang), 3, phase, 90);

        UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

        if (phase == 0)
        {
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), year, YEAR_MIN, YEAR_MAX, "");
        }
        else if (phase == 1)
        {
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), month, 1, 12, UIStrings::MonthSuffix(lang));
        }
        else
        {
            uint8_t max_day = SysTime_DaysInMonth(year, month);
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), day, 1, max_day, UIStrings::DaySuffix(lang));
        }

        UIFrame::DrawTip(UIStrings::TimeEditTip(lang));
        HAL_Screen_Update();
    }

public:
    /** 进入页面时读取当前日期；若仍是 1970 兜底日期，则默认给用户 2026-01-01。 */
    void onCreate() override
    {
        struct tm info;
        SysTime_GetInfo(&info);

        uint16_t current_year = (uint16_t)(info.tm_year + 1900);
        if (current_year < YEAR_MIN || current_year > YEAR_MAX)
        {
            year = 2026;
            month = 1;
            day = 1;
        }
        else
        {
            year = current_year;
            month = (uint8_t)(info.tm_mon + 1);
            day = (uint8_t)info.tm_mday;
            clampDay();
        }

        phase = 0;
        linkAnim.jumpTo(phase);
        drawUI();
    }

    void onResume() override { drawUI(); }

    /** 推进日期滚轮与顶部链路动画。 */
    void onLoop() override
    {
        bool dial_changed = dialAnim.update();
        bool link_changed = linkAnim.update(phase);

        if (dial_changed || link_changed)
            drawUI();
    }

    void onDestroy() override {}

    /** 旋钮调整当前字段；年/月变化时立即钳制日字段。 */
    void onKnob(int delta) override
    {
        if (phase == 0)
        {
            int next = (int)year + delta;
            if (next < YEAR_MIN) next = YEAR_MAX;
            if (next > YEAR_MAX) next = YEAR_MIN;
            year = (uint16_t)next;
            clampDay();
        }
        else if (phase == 1)
        {
            int next = (int)month + delta;
            if (next < 1) next = 12;
            if (next > 12) next = 1;
            month = (uint8_t)next;
            clampDay();
        }
        else
        {
            int max_day = SysTime_DaysInMonth(year, month);
            int next = (int)day + delta;
            if (next < 1) next = max_day;
            if (next > max_day) next = 1;
            day = (uint8_t)next;
        }

        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    /** 短按推进字段；在日期阶段保存完整年月日并返回。 */
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();

        if (phase < 2)
        {
            phase++;
            drawUI();
            return;
        }

        SysTime_SetDate(year, month, day);
        appManager.popApp();
    }

    /** 长按回退字段；在年份阶段退出不保存。 */
    void onKeyLong() override
    {
        SYS_SOUND_NAV();

        if (phase > 0)
        {
            phase--;
            drawUI();
        }
        else
        {
            appManager.popApp();
        }
    }
};

AppTimeDateSet instanceTimeDateSet;
AppBase *appTimeDateSet = &instanceTimeDateSet;

/**
 * 时间设置一级菜单。
 *
 * 菜单项：
 * 0 当前时间：只读显示 YYYY-MM-DD HH:MM，不执行设置动作；
 * 1 设置当日时间：进入 AppTimeManualSet；
 * 2 日期设置：进入 AppTimeDateSet；
 * 3 网络校时：复用 AppNetworkSync，立即执行一轮公共联网周期。
 */
class AppTimeSetting : public AppMenuBase
{
protected:
    int getMenuCount() override { return 4; }

    const char *getTitle() override
    {
        return UIStrings::TimeSettingTitle(appManager.getLanguage());
    }

    /** 返回时间设置菜单文本；周期联网策略不再暴露在时间页面。 */
    const char *getItemText(int index) override
    {
        static char buf[64];
        SystemLang_t lang = appManager.getLanguage();

        if (index == 0)
        {
            struct tm current = {};
            SysTime_GetInfo(&current);
            snprintf(buf, sizeof(buf), "%s%04d-%02d-%02d %02d:%02d",
                     UIStrings::CurrentTimeLabel(lang),
                     current.tm_year + 1900,
                     current.tm_mon + 1,
                     current.tm_mday,
                     current.tm_hour,
                     current.tm_min);
            return buf;
        }

        return UIStrings::TimeSettingItem(lang, index);
    }

    /**
     * 短按动作。
     *
     * 当前时间只读，其余三项分别进入时分、日期和立即网络同步页面。
     */
    void onItemClicked(int index) override
    {
        if (index == 0)
        {
            /* 当前时间条目只负责展示；短按不进入页面，也不改动任何时间源。 */
            drawMenuUI(visual_selection);
        }
        else if (index == 1)
        {
            appManager.push(AppId::TimeManualSet);
        }
        else if (index == 2)
        {
            appManager.push(AppId::TimeDateSet);
        }
        else if (index == 3)
        {
            appManager.push(AppId::NetworkSync);
        }
    }

    /** 长按返回系统设置；本页不再维护网络周期策略。 */
    void onLongPressed() override
    {
        appManager.popApp();
    }
};

AppTimeSetting instanceTimeSetting;
AppBase *appTimeSetting = &instanceTimeSetting;
