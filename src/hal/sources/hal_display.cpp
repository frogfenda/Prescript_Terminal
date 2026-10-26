/*
【模块职责】逻辑画布、字体和 NV3007 推屏。
【边界】这里只拥有显示缓冲区和绘图原语，不读取文件、不处理输入、不唤醒 SYS 服务。
*/
#include "hal/hal.h"
#include "hal/hal_display_config.h"
#include "hal/hal_font_config.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include <string.h>

#include "bsp/bsp_display_nv3007.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite textSprite = TFT_eSprite(&tft);

namespace
{
    U8g2_for_TFT_eSPI g_u8f;

    static inline int16_t DisplayRawX(int16_t logical_x)
    {
        return logical_x + HAL::DisplayConfig::RAM_OFFSET_X;
    }

    static inline int16_t DisplayRawY(int16_t logical_y)
    {
        return logical_y + HAL::DisplayConfig::RAM_OFFSET_Y;
    }

    static UIFontConfig::FontSpec GetFontSpec(HALFontRole role)
    {
        switch (role)
        {
        case HAL_FONT_SMALL:
            return UIFontConfig::Small();
        case HAL_FONT_TITLE:
            return UIFontConfig::Title();
        case HAL_FONT_BODY:
        default:
            return UIFontConfig::Body();
        }
    }

    static void ApplyFontRole(HALFontRole role)
    {
        const UIFontConfig::FontSpec spec = GetFontSpec(role);
        g_u8f.setFont(spec.font);
        // U8g2 切换字体后会重置透明模式，必须在每次切换后恢复。
        g_u8f.setFontMode(1);
    }
}

void HAL_Display_Init()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    const bool display_ok = BSP::DisplayNv3007::Begin();
    if (!display_ok)
        BSP::DisplayNv3007::PrintDiagnostics();

    textSprite.setColorDepth(16);
    const uint16_t sw = HAL_Get_Screen_Width();
    const uint16_t sh = HAL_Get_Screen_Height();
    void *ptr = textSprite.createSprite(sw, sh);
    if (ptr == nullptr)
    {
        Serial.printf("[显示] Sprite 创建失败：需要约 %lu 字节，当前画布=%ux%u。\n",
                      (unsigned long)sw * sh * 2UL, sw, sh);
    }
    else
    {
        Serial.printf("[显示] Sprite 创建成功：画布=%ux%u，约 %lu 字节。\n",
                      sw, sh, (unsigned long)sw * sh * 2UL);
    }

    textSprite.fillSprite(TFT_BLACK);
    textSprite.setTextWrap(false);
    g_u8f.begin(textSprite);
    g_u8f.setFontMode(1);
    g_u8f.setFontDirection(0);
    g_u8f.setBackgroundColor(TFT_BLACK);
    g_u8f.setFont(UIFontConfig::Body().font);
}

void HAL_Screen_Clear()
{
    BSP::DisplayNv3007::FillScreen(TFT_BLACK);
    textSprite.fillSprite(TFT_BLACK);
}

int HAL_Get_Font_Baseline(HALFontRole role)
{
    return GetFontSpec(role).baseline;
}

int HAL_Get_Font_Line_Height(HALFontRole role)
{
    return GetFontSpec(role).lineHeight;
}

int HAL_Get_Text_Width_Font(const char *str, HALFontRole role)
{
    if (!str)
        return 0;
    ApplyFontRole(role);
    return g_u8f.getUTF8Width(str);
}

int HAL_Get_Text_Width_Small(const char *str)
{
    return HAL_Get_Text_Width_Font(str, HAL_FONT_SMALL);
}

void HAL_Screen_ShowLine_Font(int32_t x, int32_t y, const char *str, HALFontRole role, uint16_t color)
{
    if (!str)
        return;
    ApplyFontRole(role);
    g_u8f.setForegroundColor(color);
    g_u8f.setCursor(x, y + HAL_Get_Font_Baseline(role));
    g_u8f.print(str);
}

void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char *str)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, TFT_CYAN);
}

void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char *str)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, TFT_CYAN);
}

int HAL_Get_Text_Width(const char *str)
{
    return HAL_Get_Text_Width_Font(str, HAL_FONT_BODY);
}

void HAL_Screen_ShowSmallLine(int32_t x, int32_t y, const char *str)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_SMALL, TFT_CYAN);
}

void HAL_Screen_ShowSmallLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_SMALL, color);
}

void HAL_Screen_ShowChineseLine_Faded(int32_t x, int32_t y, const char *str, float distance)
{
    HAL_Screen_ShowChineseLine_Faded_Color(x, y, str, distance, TFT_CYAN);
}

void HAL_Screen_ShowChineseLine_Faded_Color(int32_t x, int32_t y, const char *str,
                                            float distance, uint16_t base_color)
{
    float intensity = 1.0f - (distance * 0.40f);
    if (intensity < 0.15f)
        intensity = 0.15f;

    const uint8_t r8 = ((base_color >> 11) & 0x1F) * 255 / 31;
    const uint8_t g8 = ((base_color >> 5) & 0x3F) * 255 / 63;
    const uint8_t b8 = (base_color & 0x1F) * 255 / 31;
    const uint8_t final_r = (uint8_t)(r8 * intensity);
    const uint8_t final_g = (uint8_t)(g8 * intensity);
    const uint8_t final_b = (uint8_t)(b8 * intensity);
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, tft.color565(final_r, final_g, final_b));
}

void HAL_Screen_ShowChineseLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, color);
}

void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, color);
}

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels)
{
    textSprite.scroll(0, -scroll_pixels);
}

void HAL_Screen_Update()
{
    uint16_t *sprite_ptr = (uint16_t *)textSprite.getPointer();
    if (!sprite_ptr)
        return;

    BSP::DisplayNv3007::PushImageRotated(HAL::DisplayConfig::ROTATION,
                                         DisplayRawX(HAL::DisplayConfig::PUSH_X),
                                         DisplayRawY(HAL::DisplayConfig::PUSH_Y),
                                         HAL_Get_Screen_Width(),
                                         HAL_Get_Screen_Height(),
                                         sprite_ptr,
                                         HAL_Get_Screen_Width(),
                                         true);
}

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.drawLine(x0, y0, x1, y1, color);
}

void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.drawRect(x, y, w, h, color);
}

void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.fillRect(x, y, w, h, color);
}

void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       int32_t x2, int32_t y2, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color)
{
    textSprite.drawPixel(x, y, color);
}

void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0)
        return;

    const uint16_t sw = HAL_Get_Screen_Width();
    const uint16_t sh = HAL_Get_Screen_Height();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= sw || y >= sh)
        return;
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0)
        return;

    uint16_t *sprite_ptr = (uint16_t *)textSprite.getPointer();
    if (!sprite_ptr)
        return;

    BSP::DisplayNv3007::PushImageRotated(HAL::DisplayConfig::ROTATION,
                                         DisplayRawX(x + HAL::DisplayConfig::PUSH_X),
                                         DisplayRawY(y + HAL::DisplayConfig::PUSH_Y),
                                         (uint16_t)w,
                                         (uint16_t)h,
                                         sprite_ptr + (size_t)y * sw + x,
                                         sw,
                                         true);
}

void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
    textSprite.setSwapBytes(true);
    textSprite.pushImage(x, y, w, h, data);
    textSprite.setSwapBytes(false);
}

void HAL_Sprite_CopyRawRow(uint16_t y, const uint8_t *data, size_t byte_count)
{
    if (!data || y >= HAL_Get_Screen_Height())
        return;
    const size_t row_bytes = (size_t)HAL_Get_Screen_Width() * sizeof(uint16_t);
    if (byte_count > row_bytes)
        byte_count = row_bytes;
    uint8_t *sprite = (uint8_t *)textSprite.getPointer();
    if (!sprite)
        return;
    memcpy(sprite + (size_t)y * row_bytes, data, byte_count);
}

void HAL_Sprite_Clear()
{
    textSprite.fillSprite(TFT_BLACK);
}

uint16_t HAL_Get_Screen_Width()
{
    return HAL::DisplayConfig::LOGICAL_WIDTH;
}

uint16_t HAL_Get_Screen_Height()
{
    return HAL::DisplayConfig::LOGICAL_HEIGHT;
}
