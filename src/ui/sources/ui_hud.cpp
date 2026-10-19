/*
【模块职责】菜单 HUD 实现。
左侧 HUD 负责显示电池、页面标题、当前时间、BUS、TMR 及番茄专注/休息状态。

新屏字体策略：
- 页面标题使用正文/标题字体，保持主菜单层级；
- 时间、BUS、TMR 使用小字体，避免左侧 HUD 过宽挤压右侧滚轮菜单；
- HUD 宽度根据标题和状态文字宽度动态计算。
*/
#include "ui/ui_hud.h"
#include "hal/hal.h"
#include "sys/sys_power.h"
#include "sys/sys_time.h"
#include "sys/sys_runtime_status.h"
#include "ui/ui_theme.h"
#include "sys/app_manager.h"
#include "sys/sys_clock_rules.h"
#include "lang/ui_strings.h"

namespace {
constexpr int HUD_PADDING_X = 12;

// HUD 内部垂直堆叠间距使用小字体行高，避免 BUS/TMR 状态互相压住。
static inline int HudStackLineH()
{
    return HAL_Get_Font_Line_Height(HAL_FONT_SMALL) + 1;
}

// 主循环共享 HUD 只缓存最新字符串的度量。菜单滚动帧复用测宽结果，时间/状态/标题
// 实际变化时才重新调用字体遍历；缓存不改变文本内容，也不缓存过时的运行状态。
struct HudTextMetric
{
    String text;
    int width = 0;
    bool valid = false;
    int measure(const char *value, HALFontRole role)
    {
        const char *safe = value ? value : "";
        if (!valid || !text.equals(safe))
        {
            text = safe;
            width = HAL_Get_Text_Width_Font(safe, role);
            valid = true;
        }
        return width;
    }
};
HudTextMetric s_titleMetric, s_timeMetric, s_busMetric, s_timerMetric, s_pomodoroMetric, s_pomodoroReserveMetric;
}

// 【函数说明】绘制菜单左栏：顶部电池，中部标题和时间，底部 BUS/TMR 状态，并返回左栏实际宽度。
int UIHud_DrawLeftPanel(const char* title_text)
{
    sysPower.drawBatteryIcon(4, 4);

    int sh = HAL_Get_Screen_Height();
    int title_w = s_titleMetric.measure(title_text, HAL_FONT_TITLE);

    char time_str[10];
    SysTime_GetTimeString(time_str);
    int time_w = s_timeMetric.measure(time_str, HAL_FONT_SMALL);

    SysHudStatus status = SysRuntime_GetHudStatus();

    int status_w = 0;
    char bus_str[16] = {0};
    char tmr_str[18] = {0};
    char pom_str[32] = {0};

    if (status.nfc_active)
    {
        sprintf(bus_str, "[BUS %02d]", status.nfc_remaining_sec);
        status_w = max(status_w, s_busMetric.measure(bus_str, HAL_FONT_SMALL));
    }

    if (status.countdown_active)
    {
        sprintf(tmr_str, "[TMR %02d:%02d]", status.countdown_remaining_sec / 60, status.countdown_remaining_sec % 60);
        status_w = max(status_w, s_timerMetric.measure(tmr_str, HAL_FONT_SMALL));
    }
    if (status.pomodoro_active)
    {
        const auto lang=appManager.getLanguage();
        snprintf(pom_str,sizeof(pom_str),"[%s %02d:%02d]",UIStrings::PomodoroPhaseName(lang,status.pomodoro_rest ? 1 : 0),
            status.pomodoro_remaining_sec/60,status.pomodoro_remaining_sec%60);
        s_pomodoroMetric.measure(pom_str,HAL_FONT_SMALL);
        // 同一轮预留最长阶段标签与三位分钟，避免专注转休息或 100→99 分钟时左栏突然收缩。
        char reserve[32]; snprintf(reserve,sizeof(reserve),"[%s %d:00]",UIStrings::PomodoroPhaseName(lang,0),SysClockRules::WorkMaxMinutes);
        status_w=max(status_w,s_pomodoroReserveMetric.measure(reserve,HAL_FONT_SMALL));
        status_w=max(status_w,s_pomodoroMetric.width);
    }

    int max_content_w = max(max(title_w, time_w), status_w);
    int left_panel_w = max_content_w + (HUD_PADDING_X * 2);

    // 三个状态同时出现时，将标题/时间整体上移；每行至少保留 2px 间隔，不能让状态盖住时间。
    const int small_h=HAL_Get_Font_Line_Height(HAL_FONT_SMALL);
    const int status_count=(int)status.nfc_active+(int)status.countdown_active+(int)status.pomodoro_active;
    int time_y=(sh/2)+4;
    if (status_count>0) {
        const int status_top=sh-small_h-4-(status_count-1)*HudStackLineH();
        time_y=min(time_y,status_top-small_h-2);
    }
    // 一两行状态维持原来的标题位置；增加第三行时与时间保持整体间距。
    int title_x = (left_panel_w - title_w) / 2;
    int title_y = time_y - HAL_Get_Font_Line_Height(HAL_FONT_TITLE) - 8;
    HAL_Screen_ShowLine_Font(title_x, title_y, title_text, HAL_FONT_TITLE, TFT_CYAN);

    // 时间使用小字体，防止左栏过宽；时间读取走非阻塞 SysTime_GetTimeString。
    int time_x = (left_panel_w - time_w) / 2;
    HAL_Screen_ShowSmallLine(time_x, time_y, time_str);

    HAL_Draw_Line(left_panel_w, UITheme::Menu::LineMarginY(), left_panel_w, sh - UITheme::Menu::LineMarginY(), 1);

    int hud_y_offset = sh - HAL_Get_Font_Line_Height(HAL_FONT_SMALL) - 4;

    if (status.nfc_active)
    {
        int bus_w = s_busMetric.width;
        HAL_Screen_ShowSmallLine((left_panel_w - bus_w) / 2, hud_y_offset, bus_str);
        hud_y_offset -= HudStackLineH();
    }

    if (status.countdown_active)
    {
        int tmr_w = s_timerMetric.width;
        HAL_Screen_ShowSmallLine((left_panel_w - tmr_w) / 2, hud_y_offset, tmr_str);
        hud_y_offset -= HudStackLineH();
    }
    if (status.pomodoro_active)
    {
        HAL_Screen_ShowSmallLine((left_panel_w-s_pomodoroMetric.width)/2,hud_y_offset,pom_str);
    }

    return left_panel_w;
}

// 【函数说明】查询 HUD 数据是否变化；AppMenuBase 用它决定菜单是否需要因为时间/NFC/倒计时变化而重绘。
bool UIHud_NeedsRedraw()
{
    return SysRuntime_HudStatusChanged();
}
