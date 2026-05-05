#include "ui_text.h"

namespace UIText {

int Utf8CharLen(unsigned char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

int CharWidth(const char* text, int len)
{
    char buf[5] = {0};
    for (int i = 0; i < len && i < 4; ++i) buf[i] = text[i];
    int w = HAL_Get_Text_Width(buf);
    if (w <= 0 || buf[0] == ' ') return 5;
    return w;
}

int Measure(const char* text)
{
    if (!text) return 0;
    return HAL_Get_Text_Width(text);
}

void Draw(int x, int y, const char* text)
{
    if (!text) return;
    HAL_Screen_ShowChineseLine(x, y, text);
}

void DrawFaded(int x, int y, const char* text, float fade)
{
    if (!text) return;
    HAL_Screen_ShowChineseLine_Faded(x, y, text, fade);
}

void DrawCentered(int y, const char* text)
{
    int sw = HAL_Get_Screen_Width();
    HAL_Screen_ShowChineseLine((sw - Measure(text)) / 2, y, text);
}

void DrawCenteredFaded(int y, const char* text, float fade)
{
    int sw = HAL_Get_Screen_Width();
    HAL_Screen_ShowChineseLine_Faded((sw - Measure(text)) / 2, y, text, fade);
}

void DrawClipped(int x, int y, const char* text, int min_x, int max_x)
{
    if (!text) return;
    int sw = HAL_Get_Screen_Width();
    if (max_x < 0) max_x = sw;

    int cursor_x = x;
    for (int i = 0; text[i] != '\0';)
    {
        int len = Utf8CharLen((unsigned char)text[i]);
        char buf[5] = {0};
        for (int b = 0; b < len && b < 4; ++b) buf[b] = text[i + b];
        int cw = CharWidth(&text[i], len);

        if (cursor_x >= min_x && cursor_x + cw <= max_x && buf[0] != ' ')
        {
            if (len == 1) HAL_Screen_ShowTextLine(cursor_x, y, buf);
            else HAL_Screen_ShowChineseLine(cursor_x, y, buf);
        }
        cursor_x += cw;
        i += len;
        if (cursor_x > max_x) break;
    }
}

void DrawClippedFaded(int x, int y, const char* text, float fade, int min_x, int max_x)
{
    if (!text) return;
    int sw = HAL_Get_Screen_Width();
    if (max_x < 0) max_x = sw;

    int cursor_x = x;
    for (int i = 0; text[i] != '\0';)
    {
        int len = Utf8CharLen((unsigned char)text[i]);
        char buf[5] = {0};
        for (int b = 0; b < len && b < 4; ++b) buf[b] = text[i + b];
        int cw = CharWidth(&text[i], len);

        if (cursor_x >= min_x && cursor_x + cw <= max_x && buf[0] != ' ')
        {
            HAL_Screen_ShowChineseLine_Faded(cursor_x, y, buf, fade);
        }
        cursor_x += cw;
        i += len;
        if (cursor_x > max_x) break;
    }
}

}
