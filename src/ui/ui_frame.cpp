#include "ui_frame.h"

namespace UIFrame {

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

void DrawTip(const char* text, int y, float fade)
{
    UIText::DrawCenteredFaded(y, text, fade);
}

void DrawDangerConfirm(const char* title, const char* message, const char* tip)
{
    int sw = HAL_Get_Screen_Width();
    HAL_Screen_ShowChineseLine(UITheme::Dialog::TitleX, UITheme::Dialog::TextY, title);
    HAL_Screen_ShowChineseLine(sw - HAL_Get_Text_Width(message) - UITheme::Dialog::RightPadX,
                               UITheme::Dialog::TextY,
                               message);
    DrawTip(tip, UITheme::Dialog::TipY, UITheme::Dialog::TipFade);
}

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
