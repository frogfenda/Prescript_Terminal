/*
【模块职责】UI 框架图形实现。

本文件绘制所有设置流页面共用的基础机械结构：
- 中部折线分隔条；
- 底部操作提示；
- 危险确认弹窗；
- 当前编辑区域角框。

新屏分支中，这些结构不再写死旧 284×76 的中心缺口和角线尺寸，
而是通过 UITheme::Frame 按当前 428×142 画布计算，保证后续屏幕尺寸变化时优先改主题层。
*/
#include "ui/ui_frame.h"
#include <math.h>

namespace UIFrame {

void DrawFloatingSelection(int x, int y, int w, int h, float reflection)
{
    if (w <= 2 || h <= 2)
        return;
    reflection = constrain(reflection, -1.0f, 1.0f);
    const int right = x + w - 1;
    const int bottom = y + h - 1;
    const int depth = UITheme::Frame::SelectionDepth();
    // 同一青色按亮度区分朝光面与背光面；不加第二套配色或文字残影。
    auto shade = [](float strength) -> uint16_t {
        return ((uint16_t)(63.0f * strength) << 5) | (uint16_t)(31.0f * strength);
    };
    const uint16_t top_light = shade(0.82f + reflection * 0.16f);
    const uint16_t bottom_light = shade(0.43f - reflection * 0.22f);
    const uint16_t left_light = shade(0.74f - reflection * 0.14f);
    const uint16_t right_light = shade(0.46f + reflection * 0.20f);

    // 后缘始终向右下偏移 2～3px，通过连接角线表达薄面板厚度；前框位置不随之移动。
    HAL_Draw_Line(x + depth, bottom + depth, right + depth, bottom + depth, shade(0.15f));
    HAL_Draw_Line(right + depth, y + depth, right + depth, bottom + depth, shade(0.20f));
    HAL_Draw_Line(x, bottom, x + depth, bottom + depth, shade(0.23f));
    HAL_Draw_Line(right, bottom, right + depth, bottom + depth, shade(0.25f));
    HAL_Draw_Line(right, y, right + depth, y + depth, shade(0.30f));
    HAL_Draw_Rect(x - 2, y - 2, w + 4, h + 4, UITheme::COLOR_DIM);
    HAL_Draw_Line(x, y, right, y, top_light);
    HAL_Draw_Line(x, bottom, right, bottom, bottom_light);
    HAL_Draw_Line(x, y, x, bottom, left_light);
    HAL_Draw_Line(right, y, right, bottom, right_light);

    // 短反光带沿上下框边柔和偏移，不进入正文；反向滚动时两侧亮度连续交换。
    const int glint_width = min(40, max(8, w / 6));
    const int glint_left = x + w / 3 + (int)lroundf(reflection * w * 0.12f);
    HAL_Draw_Line(glint_left, y - 1, glint_left + glint_width, y - 1, shade(0.68f + reflection * 0.28f));
    HAL_Draw_Line(glint_left, bottom + 1, glint_left + glint_width, bottom + 1, shade(0.40f - reflection * 0.28f));

    // 沿用上一版被认可的四角定位线，避免厚度和反光削弱选中区域的辨识度。
    const int bracket = min(9, max(5, h / 3));
    HAL_Draw_Line(x - 3, y, x + bracket, y, 1);
    HAL_Draw_Line(x - 3, y, x - 3, y + bracket, 1);
    HAL_Draw_Line(x + w + 2, y, x + w - bracket, y, 1);
    HAL_Draw_Line(x + w + 2, y, x + w + 2, y + bracket, 1);
    HAL_Draw_Line(x - 3, y + h, x + bracket, y + h, 1);
    HAL_Draw_Line(x - 3, y + h, x - 3, y + h - bracket, 1);
    HAL_Draw_Line(x + w + 2, y + h, x + w - bracket, y + h, 1);
    HAL_Draw_Line(x + w + 2, y + h, x + w + 2, y + h - bracket, 1);
}

// 【函数说明】绘制中间下折的战术分隔线。
// 画面效果：左右两条水平线在屏幕中心让出一个小台阶，形成“仪表切口”。
// 实现步骤：
// 1. 根据当前屏宽求中心点；
// 2. 从 UITheme 获取中心缺口半宽、斜角宽度和斜角高度；
// 3. 先画左右水平段，再画中心两段斜线和中间短水平段。
void DrawTacticalDivider(int y, uint16_t color)
{
    if (y < 0)
        y = UITheme::EditFlow::DividerY();

    int sw = HAL_Get_Screen_Width();
    int cx = sw / 2;
    int half_gap = UITheme::Frame::DividerCenterHalfGap();
    int bevel_w = UITheme::Frame::DividerBevelW();
    int bevel_h = UITheme::Frame::DividerBevelH();

    HAL_Draw_Line(0, y, cx - half_gap, y, color);
    HAL_Draw_Line(cx - half_gap, y, cx - half_gap + bevel_w, y + bevel_h, color);
    HAL_Draw_Line(cx - half_gap + bevel_w, y + bevel_h, cx + half_gap - bevel_w, y + bevel_h, color);
    HAL_Draw_Line(cx + half_gap - bevel_w, y + bevel_h, cx + half_gap, y, color);
    HAL_Draw_Line(cx + half_gap, y, sw, y, color);
}

// 【函数说明】在底部居中绘制操作提示。
// 提示文字统一使用 UIText 的淡出绘制，与菜单弱化文字保持同一种视觉层级。
void DrawTip(const char* text, int y, float fade)
{
    if (y < 0)
        y = UITheme::EditFlow::TipY();
    if (fade < 0.0f)
        fade = UITheme::EditFlow::TipFade();

    UIText::DrawCenteredFaded(y, text, fade);
}

// 【函数说明】绘制通用状态弹窗，调用方通过颜色区分成功、普通提示或错误。
// 本函数只负责弹窗文字和提示，不主动清屏、不主动推屏，调用方可决定是否叠加其他图形。
// 弹窗样式统一采用“指令档案”原有的居中黑底框，避免不同 App 各画一套确认框。
void DrawDialog(const char* title, const char* message, const char* tip, uint16_t color)
{
    int sw = HAL_Get_Screen_Width();
    int sh = HAL_Get_Screen_Height();
    int body_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
    int small_h = HAL_Get_Font_Line_Height(HAL_FONT_SMALL);
    bool has_message = message && message[0] != '\0';

    int box_w = sw - 72;
    if (box_w < 180)
        box_w = sw - 24;
    int box_h = has_message ? max(64, body_h * 2 + small_h + 20) : max(52, body_h + small_h + 20);
    int box_x = (sw - box_w) / 2;
    int box_y = (sh - box_h) / 2;

    HAL_Fill_Rect(box_x, box_y, box_w, box_h, TFT_BLACK);
    HAL_Draw_Rect(box_x, box_y, box_w, box_h, color);

    int title_w = HAL_Get_Text_Width(title);
    HAL_Screen_ShowLine_Font((sw - title_w) / 2, box_y + 9, title, HAL_FONT_BODY, color);

    if (has_message)
    {
        int msg_w = HAL_Get_Text_Width(message);
        HAL_Screen_ShowLine_Font((sw - msg_w) / 2, box_y + 9 + body_h + 4, message, HAL_FONT_BODY, color);
    }

    int tip_w = HAL_Get_Text_Width_Font(tip, HAL_FONT_SMALL);
    HAL_Screen_ShowLine_Font((sw - tip_w) / 2,
                             box_y + box_h - small_h - 7,
                             tip,
                             HAL_FONT_SMALL,
                             TFT_DARKGREY);
}

// 【函数说明】保留原危险确认接口，旧页面继续得到红色弹窗而无需改调用点。
void DrawDangerConfirm(const char* title, const char* message, const char* tip)
{
    DrawDialog(title, message, tip, TFT_RED);
}

// 【函数说明】绘制无填充角框，突出当前滚轮/确认区域。
// x1/x2 是左右边界，center_y 是文字基线附近位置，half_w 保留为兼容旧接口；
// 实际角线长度由 UITheme::Frame::CornerSize() 给出，避免新屏上角标过小。
void DrawCornerBox(int x1, int x2, int center_y, int half_w, int h, uint16_t color)
{
    if (h < 0)
        h = UITheme::Dial::BoxHeight();

    int corner = UITheme::Frame::CornerSize();
    int top_y = center_y - 2;
    int bot_y = center_y + h - 2;

    HAL_Draw_Line(x1, top_y, x1 + corner, top_y, color);
    HAL_Draw_Line(x1, top_y, x1, top_y + corner, color);
    HAL_Draw_Line(x1, bot_y, x1 + corner, bot_y, color);
    HAL_Draw_Line(x1, bot_y, x1, bot_y - corner, color);

    HAL_Draw_Line(x2, top_y, x2 - corner, top_y, color);
    HAL_Draw_Line(x2, top_y, x2, top_y + corner, color);
    HAL_Draw_Line(x2, bot_y, x2 - corner, bot_y, color);
    HAL_Draw_Line(x2, bot_y, x2, bot_y - corner, color);
}

}
