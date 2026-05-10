/*
【模块职责】时间设置页面组。

本文件定义三个页面：
1. AppTimeSetting：时间设置一级菜单，使用 AppMenuBase 的滚轮菜单；
2. AppTimeManualSet：设置“当日时分”，使用 TacticalLinkEngine + DialAnimator 链路式编辑；
3. AppTimeDateSet：设置“当前日期”，同样使用链路式编辑。

本轮调整点：
- “设置当日时间”后面新增“日期设置”；
- 当日时间设置改为直接 settimeofday 到指定时分，不做偏置叠加；
- “周期校时”和“校时间隔”改为复用指令推送配置那套三段式菜单值编辑动画；
- 三段式动画引擎已经从 AppBase 移到 src/ui/ui_value_animator.h，AppBase 只保留兼容封装。

交互约定：
- 一级菜单：短按进入/编辑，长按保存策略并返回；
- 链路编辑页：短按下一步/保存，长按上一步/返回；
- 旋钮：菜单选项切换或字段数值调整。
*/
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"
#include "sys_network.h"
#include "sys_time.h"
#include "sys/sys_audio.h"
#include "../ui/ui_frame.h"

/*
 * 周期校时的可选间隔。
 *
 * 不开放任意分钟输入，原因是：
 * - 旋钮小屏操作更快；
 * - 避免用户误设 1 分钟导致 WiFi 频繁唤醒；
 * - 后台 Network_Update 只需要处理有限的策略档位。
 */
static const uint16_t kResyncIntervalsMin[] = {5, 15, 30, 60};
static constexpr int kResyncIntervalCount = sizeof(kResyncIntervalsMin) / sizeof(kResyncIntervalsMin[0]);

/** 根据配置中的分钟数找到预设下标；异常值回退到 15 分钟。 */
static int _FindIntervalIndex(uint16_t min_value)
{
    for (int i = 0; i < kResyncIntervalCount; i++)
    {
        if (kResyncIntervalsMin[i] == min_value)
            return i;
    }

    return 1; // 默认 15 分钟
}

/** 把下标按 [0, count) 循环，供旋钮连续滚动使用。 */
static int _WrapIndex(int value, int count)
{
    while (value < 0) value += count;
    while (value >= count) value -= count;
    return value;
}

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
        bool zh = appManager.getLanguage() == LANG_ZH;

        const char *names_zh[] = {"设定小时", "设定分钟"};
        const char *names_en[] = {"SET HOUR", "SET MIN"};
        linkAnim.draw(UITheme::EditFlow::LinkY(), zh ? names_zh : names_en, 2, phase, 95);

        UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

        if (phase == 0)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), hour, 0, 23, "");
        else
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), minute, 0, 59, "");

        UIFrame::DrawTip(zh ? "短按下一步/保存  长按返回" : "CLICK: NEXT/SAVE  LONG: BACK");
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
        bool zh = appManager.getLanguage() == LANG_ZH;

        const char *names_zh[] = {"设定年份", "设定月份", "设定日期"};
        const char *names_en[] = {"SET YEAR", "SET MONTH", "SET DAY"};
        linkAnim.draw(UITheme::EditFlow::LinkY(), zh ? names_zh : names_en, 3, phase, 90);

        UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

        if (phase == 0)
        {
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), year, YEAR_MIN, YEAR_MAX, "");
        }
        else if (phase == 1)
        {
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), month, 1, 12, zh ? "月" : "M");
        }
        else
        {
            uint8_t max_day = SysTime_DaysInMonth(year, month);
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), day, 1, max_day, zh ? "日" : "D");
        }

        UIFrame::DrawTip(zh ? "短按下一步/保存  长按返回" : "CLICK: NEXT/SAVE  LONG: BACK");
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
 * 0 设置当日时间：进入 AppTimeManualSet；
 * 1 日期设置：进入 AppTimeDateSet；
 * 2 网络校时：复用 AppNetworkSync，执行完整同步 NTP + API；
 * 3 周期校时：使用三段式菜单值编辑动画，在本页直接开关；
 * 4 校时间隔：使用三段式菜单值编辑动画，在本页选择 5/15/30/60 分钟。
 */
class AppTimeSetting : public AppMenuBase
{
private:
    bool is_editing = false;
    bool temp_auto_resync = true;
    int temp_interval_idx = 1;

    /** 把当前临时策略写入 sysConfig。 */
    void savePolicy()
    {
        sysConfig.time_auto_resync = temp_auto_resync;
        sysConfig.time_resync_interval_min = kResyncIntervalsMin[temp_interval_idx];
        sysConfig.save();
    }

protected:
    int getMenuCount() override { return 5; }

    const char *getTitle() override
    {
        return (appManager.getLanguage() == LANG_ZH) ? "时间设置" : "TIME CONFIG";
    }

    /** 返回菜单项文本；编辑中的条目会在末尾显示“<”提示。 */
    const char *getItemText(int index) override
    {
        static char buf[64];
        bool zh = appManager.getLanguage() == LANG_ZH;
        const char *edit_mark = (is_editing && index == current_selection) ? " <" : "";

        if (index == 0)
            return zh ? "设置当日时间" : "SET TODAY TIME";

        if (index == 1)
            return zh ? "日期设置" : "SET DATE";

        if (index == 2)
            return zh ? "网络校时" : "NETWORK SYNC";

        if (index == 3)
        {
            sprintf(buf,
                    zh ? "周期校时: %s%s" : "AUTO RESYNC: %s%s",
                    temp_auto_resync ? (zh ? "开启" : "ON") : (zh ? "关闭" : "OFF"),
                    edit_mark);
            return buf;
        }

        sprintf(buf,
                zh ? "校时间隔: %u 分钟%s" : "SYNC PERIOD: %u MIN%s",
                (unsigned)kResyncIntervalsMin[temp_interval_idx],
                edit_mark);
        return buf;
    }

    /**
     * 为周期校时和校时间隔提供三段式动态值。
     *
     * AppMenuBase 会调用 UIValueAnimator，只让中间的“开启/15”等值跳动，
     * 这和指令推送配置里的开关、最短潜伏时间编辑效果一致。
     */
    bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix) override
    {
        if (!is_editing || index != current_selection)
            return false;

        static char pref[32];
        static char val[16];
        static char suff[24];
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (index == 3)
        {
            strcpy(pref, zh ? "周期校时: " : "AUTO RESYNC: ");
            strcpy(val, temp_auto_resync ? (zh ? "开启" : "ON") : (zh ? "关闭" : "OFF"));
            strcpy(suff, " <");
        }
        else if (index == 4)
        {
            strcpy(pref, zh ? "校时间隔: " : "SYNC PERIOD: ");
            sprintf(val, "%u", (unsigned)kResyncIntervalsMin[temp_interval_idx]);
            strcpy(suff, zh ? " 分钟 <" : " MIN <");
        }
        else
        {
            return false;
        }

        *prefix = pref;
        *anim_val = val;
        *suffix = suff;
        return true;
    }

    /**
     * 短按动作。
     *
     * - 普通条目进入对应页面；
     * - 周期校时/校时间隔进入或退出编辑状态；
     * - 退出编辑状态时立即保存策略，避免用户忘记长按返回导致设置丢失。
     */
    void onItemClicked(int index) override
    {
        if (is_editing)
        {
            is_editing = false;
            savePolicy();
            drawMenuUI(visual_selection);
            return;
        }

        if (index == 0)
        {
            appManager.push(AppId::TimeManualSet);
        }
        else if (index == 1)
        {
            appManager.push(AppId::TimeDateSet);
        }
        else if (index == 2)
        {
            appManager.push(AppId::NetworkSync);
        }
        else if (index == 3 || index == 4)
        {
            is_editing = true;
            drawMenuUI(visual_selection);
        }
    }

    /** 长按保存当前周期校时策略并返回系统设置。 */
    void onLongPressed() override
    {
        savePolicy();
        is_editing = false;
        appManager.popApp();
    }

public:
    /** 进入页面时从 sysConfig 读取策略到临时变量，编辑期间先不立即污染配置。 */
    void onCreate() override
    {
        is_editing = false;
        temp_auto_resync = sysConfig.time_auto_resync;
        temp_interval_idx = _FindIntervalIndex(sysConfig.time_resync_interval_min);
        AppMenuBase::onCreate();
    }

    /**
     * 旋钮处理。
     *
     * 编辑状态：
     * - 周期校时：任意旋钮步进都会翻转 ON/OFF；
     * - 校时间隔：在 5/15/30/60 分钟之间循环。
     *
     * 非编辑状态：交给 AppMenuBase 做滚轮菜单切换。
     */
    void onKnob(int delta) override
    {
        if (is_editing)
        {
            if (current_selection == 3)
            {
                temp_auto_resync = !temp_auto_resync;
            }
            else if (current_selection == 4)
            {
                temp_interval_idx = _WrapIndex(temp_interval_idx + delta, kResyncIntervalCount);
            }

            SYS_SOUND_GLITCH();
            triggerEditAnimation(delta);
            drawMenuUI(visual_selection);
            return;
        }

        AppMenuBase::onKnob(delta);
    }
};

AppTimeSetting instanceTimeSetting;
AppBase *appTimeSetting = &instanceTimeSetting;
