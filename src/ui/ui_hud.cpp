/*
【模块职责】菜单 HUD 实现。
左侧 HUD 负责显示电池、页面标题、当前时间、NFC 伪装 BUS 状态和倒计时 TMR 状态。

新屏字体策略：
- 页面标题使用正文/标题字体，保持主菜单层级；
- 时间、BUS、TMR 使用小字体，避免左侧 HUD 过宽挤压右侧滚轮菜单；
- HUD 宽度根据标题和状态文字宽度动态计算。
*/
#include "ui_hud.h"
#include "../hal/hal.h"
#include "../sys/sys_power.h"
#include "../sys/sys_time.h"
#include "../sys/sys_runtime_status.h"
#include "ui_theme.h"

namespace {
constexpr int HUD_PADDING_X = 12;

// HUD 内部垂直堆叠间距使用小字体行高，避免 BUS/TMR 状态互相压住。
static inline int HudStackLineH()
{
    return HAL_Get_Font_Line_Height(HAL_FONT_SMALL) + 1;
}
}

// 【函数说明】绘制菜单左栏：顶部电池，中部标题和时间，底部 BUS/TMR 状态，并返回左栏实际宽度。
int UIHud_DrawLeftPanel(const char* title_text)
{
    sysPower.drawBatteryIcon(4, 4);

    int sh = HAL_Get_Screen_Height();
    int title_w = HAL_Get_Text_Width_Font(title_text, HAL_FONT_TITLE);

    char time_str[10];
    SysTime_GetTimeString(time_str);
    int time_w = HAL_Get_Text_Width_Small(time_str);

    SysHudStatus status = SysRuntime_GetHudStatus();

    int status_w = 0;
    char bus_str[16] = {0};
    char tmr_str[18] = {0};

    if (status.nfc_active)
    {
        sprintf(bus_str, "[BUS %02d]", status.nfc_remaining_sec);
        status_w = max(status_w, HAL_Get_Text_Width_Small(bus_str));
    }

    if (status.countdown_active)
    {
        sprintf(tmr_str, "[TMR %02d:%02d]", status.countdown_remaining_sec / 60, status.countdown_remaining_sec % 60);
        status_w = max(status_w, HAL_Get_Text_Width_Small(tmr_str));
    }

    int max_content_w = max(max(title_w, time_w), status_w);
    int left_panel_w = max_content_w + (HUD_PADDING_X * 2);

    // 标题放在 HUD 中部偏上，使用标题角色，字号随 ui_font_config.h 配置变化。
    int title_x = (left_panel_w - title_w) / 2;
    int title_y = (sh / 2) - (HAL_Get_Font_Line_Height(HAL_FONT_TITLE) + 4);
    HAL_Screen_ShowLine_Font(title_x, title_y, title_text, HAL_FONT_TITLE, TFT_CYAN);

    // 时间使用小字体，防止左栏过宽；时间读取走非阻塞 SysTime_GetTimeString。
    int time_x = (left_panel_w - time_w) / 2;
    int time_y = (sh / 2) + 4;
    HAL_Screen_ShowSmallLine(time_x, time_y, time_str);

    HAL_Draw_Line(left_panel_w, UITheme::Menu::LineMarginY(), left_panel_w, sh - UITheme::Menu::LineMarginY(), 1);

    int hud_y_offset = sh - HAL_Get_Font_Line_Height(HAL_FONT_SMALL) - 4;

    if (status.nfc_active)
    {
        int bus_w = HAL_Get_Text_Width_Small(bus_str);
        HAL_Screen_ShowSmallLine((left_panel_w - bus_w) / 2, hud_y_offset, bus_str);
        hud_y_offset -= HudStackLineH();
    }

    if (status.countdown_active)
    {
        int tmr_w = HAL_Get_Text_Width_Small(tmr_str);
        HAL_Screen_ShowSmallLine((left_panel_w - tmr_w) / 2, hud_y_offset, tmr_str);
    }

    return left_panel_w;
}

// 【函数说明】查询 HUD 数据是否变化；AppMenuBase 用它决定菜单是否需要因为时间/NFC/倒计时变化而重绘。
bool UIHud_NeedsRedraw()
{
    return SysRuntime_HudStatusChanged();
}
