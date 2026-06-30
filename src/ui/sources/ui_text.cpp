/*
【模块职责】UI 文本工具实现。逐 UTF-8 字符测量和绘制，防止中文字符被截断；裁剪绘制只显示完整落在安全区域内的字符。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "ui/ui_text.h"

namespace UIText {

// 【函数说明】按 UTF-8 首字节位模式返回当前字符长度，保证后续测量和裁剪不会切断中文。
int Utf8CharLen(unsigned char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// 【函数说明】测量单个 UTF-8 字符宽度，异常宽度和空格按 5 像素处理。
int CharWidth(const char* text, int len)
{
    char buf[5] = {0};
    for (int i = 0; i < len && i < 4; ++i) buf[i] = text[i];
    int w = HAL_Get_Text_Width(buf);
    if (w <= 0 || buf[0] == ' ') return 5;
    return w;
}

// 【函数说明】逐字符调用 CharWidth，得到与裁剪绘制一致的总宽度。
int Measure(const char* text)
{
    if (!text) return 0;
    return HAL_Get_Text_Width(text);
}

// 【函数说明】调用 HAL 的 U8g2 文本接口在指定坐标绘制完整字符串。
void Draw(int x, int y, const char* text)
{
    if (!text) return;
    HAL_Screen_ShowChineseLine(x, y, text);
}

// 【函数说明】调用 HAL 的淡出文本接口，distance 越大颜色越暗。
void DrawFaded(int x, int y, const char* text, float fade)
{
    if (!text) return;
    HAL_Screen_ShowChineseLine_Faded(x, y, text, fade);
}

// 【函数说明】先 Measure 再以屏幕中心减半宽作为 x 坐标绘制文本。
void DrawCentered(int y, const char* text)
{
    int sw = HAL_Get_Screen_Width();
    HAL_Screen_ShowChineseLine((sw - Measure(text)) / 2, y, text);
}

// 【函数说明】居中绘制带淡出效果的文本。
void DrawCenteredFaded(int y, const char* text, float fade)
{
    int sw = HAL_Get_Screen_Width();
    HAL_Screen_ShowChineseLine_Faded((sw - Measure(text)) / 2, y, text, fade);
}

// 【函数说明】逐 UTF-8 字符绘制，只把完整落入裁剪区的字符送给 HAL。
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

// 【函数说明】逐 UTF-8 字符绘制带淡出文本，并按裁剪区过滤越界字符。
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
