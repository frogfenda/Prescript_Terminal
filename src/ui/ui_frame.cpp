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
#include "ui_frame.h"

namespace UIFrame {

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

// 【函数说明】绘制删除/清空等危险操作确认画面。
// 本函数只负责弹窗文字和提示，不主动清屏、不主动推屏，调用方可决定是否叠加其他图形。
// 弹窗样式统一采用“指令档案”原有的居中黑底红框，避免不同 App 各画一套确认框。
void DrawDangerConfirm(const char* title, const char* message, const char* tip)
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
    HAL_Draw_Rect(box_x, box_y, box_w, box_h, TFT_RED);

    int title_w = HAL_Get_Text_Width(title);
    HAL_Screen_ShowLine_Font((sw - title_w) / 2, box_y + 9, title, HAL_FONT_BODY, TFT_RED);

    if (has_message)
    {
        int msg_w = HAL_Get_Text_Width(message);
        HAL_Screen_ShowLine_Font((sw - msg_w) / 2, box_y + 9 + body_h + 4, message, HAL_FONT_BODY, TFT_RED);
    }

    int tip_w = HAL_Get_Text_Width_Font(tip, HAL_FONT_SMALL);
    HAL_Screen_ShowLine_Font((sw - tip_w) / 2,
                             box_y + box_h - small_h - 7,
                             tip,
                             HAL_FONT_SMALL,
                             TFT_DARKGREY);
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
