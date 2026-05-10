/*
【模块职责】菜单 HUD 实现。左侧显示电池、标题、时间、NFC 伪装 BUS 状态和倒计时 TMR 状态，供所有 AppMenuBase 页面复用。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "ui_hud.h"
#include "../hal/hal.h"
#include "../sys/sys_power.h"
#include "../sys/sys_time.h"
#include "../sys/sys_runtime_status.h"

namespace {
constexpr int HUD_PADDING_X = 10;
constexpr int HUD_LINE_MARGIN_Y = 8;
constexpr int HUD_STACK_LINE_H = 14;
}

// 【函数说明】绘制菜单左栏：电池图标在顶部，标题居中区域，时间在中部，BUS/TMR 状态在底部，并缓存本帧状态。
int UIHud_DrawLeftPanel(const char* title_text)
{
    sysPower.drawBatteryIcon(4, 4);

    int sh = HAL_Get_Screen_Height();
    int title_w = HAL_Get_Text_Width(title_text);

    char time_str[10];
    SysTime_GetTimeString(time_str);
    int time_w = HAL_Get_Text_Width(time_str);

    int max_content_w = (title_w > time_w) ? title_w : time_w;
    int left_panel_w = max_content_w + (HUD_PADDING_X * 2);

    int title_x = (left_panel_w - title_w) / 2;
    int title_y = (sh / 2) - 20;
    HAL_Screen_ShowChineseLine(title_x, title_y, title_text);

    int time_x = (left_panel_w - time_w) / 2;
    int time_y = (sh / 2) + 4;
    HAL_Screen_ShowTextLine(time_x, time_y, time_str);

    HAL_Draw_Line(left_panel_w, HUD_LINE_MARGIN_Y, left_panel_w, sh - HUD_LINE_MARGIN_Y, 1);

    SysHudStatus status = SysRuntime_GetHudStatus();
    int hud_y_offset = sh - 14;

    if (status.nfc_active)
    {
        char bus_str[16];
        sprintf(bus_str, "[BUS %02d]", status.nfc_remaining_sec);
        int bus_w = HAL_Get_Text_Width(bus_str);
        HAL_Screen_ShowTextLine((left_panel_w - bus_w) / 2, hud_y_offset, bus_str);
        hud_y_offset -= HUD_STACK_LINE_H;
    }

    if (status.countdown_active)
    {
        char tmr_str[16];
        sprintf(tmr_str, "[TMR %02d:%02d]", status.countdown_remaining_sec / 60, status.countdown_remaining_sec % 60);
        int tmr_w = HAL_Get_Text_Width(tmr_str);
        HAL_Screen_ShowTextLine((left_panel_w - tmr_w) / 2, hud_y_offset, tmr_str);
    }

    return left_panel_w;
}

// 【函数说明】重新采样 HUD 状态并和缓存比较，任一字段变化就要求 AppMenuBase 重绘整页。
bool UIHud_NeedsRedraw()
{
    return SysRuntime_HudStatusChanged();
}
