/*
【模块职责】绘制全屏指令纸带：上方标题/电量/时间，中部连续送纸与读取角框，下方编号和运行状态。
【动画】解析临界阻尼弹簧追赶连续目标，输入可随时反向，单帧时间封顶，静止后停止刷新。
【资源】全部为 HAL 几何图元与既有字体，不分配图片、不打开文件、不增加后台任务。
*/
#include "ui/ui_paper_menu.h"
#include "ui/ui_menu_motion.h"
#include "apps/app_menu_base.h"
#include "ui/ui_text.h"
#include "ui/ui_hud.h"
#include <math.h>

namespace {
using namespace UITheme::PaperMenu;

int Wrap(int value, int count) { return (value % count + count) % count; }

// RGB565 降亮度用于纸条退入背景；没有逐像素透明混合或字体缩放。
uint16_t Dim(uint16_t color, float strength)
{
    return ((int)(((color >> 11) & 31) * strength) << 11) |
           ((int)(((color >> 5) & 63) * strength) << 5) |
           (int)((color & 31) * strength);
}

// 状态栏长标题用既有裁剪工具，完整菜单文字仍在中央纸条保留两行。
void DrawStatus(AppMenuBase &menu)
{
    const int sw = HAL_Get_Screen_Width();
    const int sh = HAL_Get_Screen_Height();
    char clock[10];
    SysTime_GetTimeString(clock);
    const int clock_w = HAL_Get_Text_Width(clock);
    UIHud_DrawBatteryIcon(8, 10);
    UIText::DrawClippedFaded(46, 5, menu.getMenuRenderTitle(), 0.5f, 46, sw - clock_w - 20);
    HAL_Screen_ShowSmallLine_Color(sw - clock_w - 8, 5, clock, Text);
    HAL_Draw_Line(8, 29, sw - 8, 29, Muted);

    char page[24];
    snprintf(page, sizeof(page), "%02d / %02d", menu.getMenuRenderSelection() + 1, menu.getMenuRenderCount());
    HAL_Screen_ShowSmallLine_Color(12, sh - 22, page, Text);
    SysHudStatus status = UIHud_GetRuntimeStatus();
    char running[40] = {};
    if (status.countdown_active && status.nfc_active)
        snprintf(running, sizeof(running), "BUS %02d  TMR %02d:%02d", status.nfc_remaining_sec,
                 status.countdown_remaining_sec / 60, status.countdown_remaining_sec % 60);
    else if (status.countdown_active)
        snprintf(running, sizeof(running), "TMR %02d:%02d", status.countdown_remaining_sec / 60,
                 status.countdown_remaining_sec % 60);
    else if (status.nfc_active)
        snprintf(running, sizeof(running), "BUS %02d", status.nfc_remaining_sec);
    if (running[0])
        HAL_Screen_ShowSmallLine_Color(sw - HAL_Get_Text_Width(running) - 10, sh - 22, running, TFT_CYAN);
}

// 一张纸条：远处只降低高度/亮度并抬高阴影，正文始终保持真实字号与朝向。
void DrawSlip(AppMenuBase &menu, int slot, float position)
{
    const int sw = HAL_Get_Screen_Width();
    const float offset = slot - position;
    const float depth = min(fabsf(offset), 1.0f);
    const float strength = 1.0f - depth * 0.58f;
    const int w = CardWidth() - (int)(depth * 14);
    const int h = CardHeight() - (int)(depth * 16);
    const int cx = sw / 2 + (int)roundf(offset * Stride());
    const int cy = CenterY() + (int)(depth * 6);
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    if (x >= sw || x + w <= 0) return;
    const int index = Wrap(slot, menu.getMenuRenderCount());
    HAL_Fill_Rect(x + 4, y + h, w - 2, 4, Dim(Edge, 0.17f));
    HAL_Fill_Rect(x, y, w, h, Dim(Paper, strength));
    HAL_Draw_Rect(x, y, w, h, Dim(Edge, strength));
    HAL_Draw_Line(x + 8, y + 2, x + w - 11, y + 2, Dim(Text, strength * 0.45f));
    // 撕孔和右上折角提供实体纸条的识别度，避免大面积纹理干扰字形。
    for (int py = y + 9; py < y + h - 5; py += 10)
    {
        HAL_Fill_Rect(x + 5, py, 3, 3, TFT_BLACK);
        HAL_Fill_Rect(x + w - 8, py, 3, 3, TFT_BLACK);
    }
    HAL_Fill_Triangle(x + w - 12, y + 1, x + w - 2, y + 1, x + w - 2, y + 11, Dim(Edge, strength));
    HAL_Draw_Line(x + 14, y + 7, x + 14, y + h - 7, Dim(Edge, strength * 0.25f));

    const char *prefix = nullptr, *value = nullptr, *suffix = nullptr;
    const bool editing = index == menu.getMenuRenderSelection() && fabsf(offset) < 0.55f &&
                         menu.getMenuRenderItemEditParts(index, &prefix, &value, &suffix);
    const int text_w = w - 44;
    if (editing)
    {
        const int edit_w = HAL_Get_Text_Width(prefix ? prefix : "") + HAL_Get_Text_Width(value ? value : "") +
                           HAL_Get_Text_Width(suffix ? suffix : "");
        if (edit_w <= text_w)
            menu.drawMenuRenderEditText(cx - edit_w / 2, cy - HAL_Get_Font_Line_Height(HAL_FONT_BODY) / 2,
                                       prefix, value, suffix);
        else
            // 很长的英文编辑项先保证完整两行可读，避免单行数值跳动覆盖纸条边缘。
            menu.drawMenuRenderLabel(index, cx, cy, text_w, TFT_CYAN);
        HAL_Draw_Line(cx - 24, y + h - 7, cx + 24, y + h - 7, TFT_CYAN);
    }
    else
    {
        uint16_t color = menu.getMenuRenderItemColor(index);
        if (color == TFT_CYAN) color = Text;
        menu.drawMenuRenderLabel(index, cx, cy, text_w, Dim(color, strength));
    }
}
}

void UIPaperMenuRenderer::reset(float position)
{
    position_ = position;
    velocity_ = 0.0f;
    last_tick_ = millis();
}

bool UIPaperMenuRenderer::update(float target, uint32_t now)
{
    const uint32_t elapsed = now - last_tick_;
    if (elapsed < UITheme::FRAME_FAST_MS) return false;
    last_tick_ = now;
    // 解析解避免显式欧拉积分在卡帧时发散；以槽位/秒为单位，目标不会取模跳圈。
    const float dt = min(elapsed, (uint32_t)48) * 0.001f;
    return UI_MenuAdvanceDamped(target, dt, 22.0f, position_, velocity_);
}

void UIPaperMenuRenderer::draw(AppMenuBase &menu, float visual_position)
{
    HAL_Sprite_Clear();
    const int sw = HAL_Get_Screen_Width();
    const int count = menu.getMenuRenderCount();
    if (count <= 0) { HAL_Screen_Update(); return; }
    // 织线随送纸轻微错动；停稳后没有常驻背景动画，也不消耗持续整屏刷新。
    const int phase = (int)roundf(visual_position * 8) % 18;
    for (int y = 39; y < HAL_Get_Screen_Height() - 26; y += 12)
        HAL_Draw_Line(0, y, sw - 1, y, Thread);
    for (int x = phase - 18; x < sw; x += 18)
        HAL_Draw_Line(x, 35, x + 20, HAL_Get_Screen_Height() - 26, Thread);
    const int base = (int)roundf(visual_position);
    // 从远到近画，中心纸条覆盖相邻纸条；单项菜单不重复同一张纸。
    for (int i = base - 2; i <= base + 2; ++i)
        if (i != base && count > 1) DrawSlip(menu, i, visual_position);
    DrawSlip(menu, base, visual_position);
    // 固定读取口只有四个角，让文字仍是主角；角框在移动纸条上方。
    const int left = (sw - CardWidth()) / 2 - 5;
    const int right = sw - left;
    const int top = CenterY() - CardHeight() / 2 - 4;
    const int bottom = CenterY() + CardHeight() / 2 + 4;
    for (int side = 0; side < 2; ++side)
    {
        const int x = side ? right : left;
        const int dx = side ? -9 : 9;
        HAL_Draw_Line(x, top, x + dx, top, Text);
        HAL_Draw_Line(x, top, x, top + 6, Text);
        HAL_Draw_Line(x, bottom, x + dx, bottom, Text);
        HAL_Draw_Line(x, bottom, x, bottom - 6, Text);
    }
    // 清除边缘被纸条覆盖的状态带；状态按原有聚合服务显示，不引入额外计时。
    HAL_Fill_Rect(0, 0, sw, 31, TFT_BLACK);
    HAL_Fill_Rect(0, HAL_Get_Screen_Height() - 25, sw, 25, TFT_BLACK);
    DrawStatus(menu);
    HAL_Screen_Update();
}
