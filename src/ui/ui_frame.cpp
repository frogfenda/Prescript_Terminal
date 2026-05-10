/*
【模块职责】UI 框架图形实现。用线段拼出中间折角分隔线、确认弹窗边界和底部操作提示，统一设置流页面的机械感。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "ui_frame.h"

namespace UIFrame {

// 【函数说明】绘制中间下折的战术分隔线：左右水平线在中心形成小台阶，给编辑页制造仪表边框感。
void DrawTacticalDivider(int y, uint16_t color)
{
    int sw = HAL_Get_Screen_Width();
    int cx = sw / 2;
    HAL_Draw_Line(0, y, cx - 30, y, color);
    HAL_Draw_Line(cx - 30, y, cx - 25, y + 3, color);
    HAL_Draw_Line(cx - 25, y + 3, cx + 25, y + 3, color);
    HAL_Draw_Line(cx + 25, y + 3, cx + 30, y, color);
    HAL_Draw_Line(cx + 30, y, sw, y, color);
}

// 【函数说明】在底部居中绘制操作提示，使用 UIText 的淡出绘制保持和菜单弱化文本一致。
void DrawTip(const char* text, int y, float fade)
{
    UIText::DrawCenteredFaded(y, text, fade);
}

// 【函数说明】绘制删除/清空确认弹窗：先清屏，再绘制标题、消息、提示和外框，最后立即推屏。
void DrawDangerConfirm(const char* title, const char* message, const char* tip)
{
    int sw = HAL_Get_Screen_Width();
    HAL_Screen_ShowChineseLine(UITheme::Dialog::TitleX, UITheme::Dialog::TextY, title);
    HAL_Screen_ShowChineseLine(sw - HAL_Get_Text_Width(message) - UITheme::Dialog::RightPadX,
                               UITheme::Dialog::TextY,
                               message);
    DrawTip(tip, UITheme::Dialog::TipY, UITheme::Dialog::TipFade);
}

// 【函数说明】围绕中心区域画四个角线，不填充内部，用于突出当前可编辑/可确认区域。
void DrawCornerBox(int x1, int x2, int center_y, int half_w, int h, uint16_t color)
{
    int top_y = center_y - 2;
    int bot_y = center_y + h - 2;
    HAL_Draw_Line(x1, top_y, x1 + 4, top_y, color); HAL_Draw_Line(x1, top_y, x1, top_y + 4, color);
    HAL_Draw_Line(x1, bot_y, x1 + 4, bot_y, color); HAL_Draw_Line(x1, bot_y, x1, bot_y - 4, color);
    HAL_Draw_Line(x2, top_y, x2 - 4, top_y, color); HAL_Draw_Line(x2, top_y, x2, top_y + 4, color);
    HAL_Draw_Line(x2, bot_y, x2 - 4, bot_y, color); HAL_Draw_Line(x2, bot_y, x2, bot_y - 4, color);
}

}
