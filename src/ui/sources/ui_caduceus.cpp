/*
【模块职责】实现双蛇杖沉浸式全屏画面及非阻塞边框反馈。
【绘制顺序】武器图片/缺失占位在底层，进度数字在其上，反馈边框最后绘制，随后由App统一推屏。
*/
#include "ui/ui_caduceus.h"

#include "hal/hal.h"
#include "ui/ui_theme.h"

namespace
{
    uint16_t ScaleRgb565(uint16_t color, uint8_t brightness)
    {
        const uint16_t r = (uint16_t)((((color >> 11) & 0x1F) * brightness) / 255U);
        const uint16_t g = (uint16_t)((((color >> 5) & 0x3F) * brightness) / 255U);
        const uint16_t b = (uint16_t)(((color & 0x1F) * brightness) / 255U);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    void DrawProgressNumbers(uint16_t progressMask)
    {
        const int screenWidth = HAL_Get_Screen_Width();
        const int lineHeight = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        const int y = HAL_Get_Screen_Height() - lineHeight - 4;
        const int cellWidth = screenWidth / UICaduceus::WEAPON_COUNT;

        for (uint8_t index = 0; index < UICaduceus::WEAPON_COUNT; ++index)
        {
            char number[2] = {(char)('1' + index), '\0'};
            const int width = HAL_Get_Text_Width_Font(number, HAL_FONT_BODY);
            const int x = index * cellWidth + (cellWidth - width) / 2;
            const bool completed = (progressMask & (uint16_t)(1U << index)) != 0;

            if (completed)
            {
                // 四向偏移形成稳定的1px青色描边，中心保持需求规定的白色数字本体。
                HAL_Screen_ShowLine_Font(x - 1, y, number, HAL_FONT_BODY, TFT_CYAN);
                HAL_Screen_ShowLine_Font(x + 1, y, number, HAL_FONT_BODY, TFT_CYAN);
                HAL_Screen_ShowLine_Font(x, y - 1, number, HAL_FONT_BODY, TFT_CYAN);
                HAL_Screen_ShowLine_Font(x, y + 1, number, HAL_FONT_BODY, TFT_CYAN);
            }
            else
            {
                // 未完成数字只加深色投影，确保白字在明亮武器图上仍可读，但不产生完成态描边。
                HAL_Screen_ShowLine_Font(x + 1, y + 1, number, HAL_FONT_BODY, TFT_BLACK);
            }
            HAL_Screen_ShowLine_Font(x, y, number, HAL_FONT_BODY, TFT_WHITE);
        }
    }
}

void UICaduceusFeedbackAnimator::trigger(uint16_t color, uint32_t nowMs, uint16_t durationMs)
{
    color_ = color;
    startedMs_ = nowMs;
    durationMs_ = durationMs > 0 ? durationMs : 1;
}

void UICaduceusFeedbackAnimator::reset()
{
    startedMs_ = 0;
    durationMs_ = 0;
    color_ = 0;
}

bool UICaduceusFeedbackAnimator::active(uint32_t nowMs) const
{
    return durationMs_ > 0 && (uint32_t)(nowMs - startedMs_) < durationMs_;
}

void UICaduceusFeedbackAnimator::draw(uint32_t nowMs) const
{
    if (!active(nowMs))
        return;

    const uint32_t elapsed = nowMs - startedMs_;
    const uint32_t faded = elapsed * 255U / durationMs_;
    const uint8_t brightness = (uint8_t)(255U - (faded < 255U ? faded : 255U));
    const uint16_t color = ScaleRgb565(color_, brightness);
    const int width = HAL_Get_Screen_Width();
    const int height = HAL_Get_Screen_Height();

    // 三层整屏细框复用“收到蓝牙消息”的蓝光语义，但保持非阻塞，动作识别与音频任务可继续运行。
    HAL_Draw_Rect(0, 0, width, height, color);
    HAL_Draw_Rect(1, 1, width - 2, height - 2, color);
    HAL_Draw_Rect(2, 2, width - 4, height - 4, color);
}

void UICaduceus::DrawWeaponFrame(const SysRgb565View &image,
                                 uint16_t progressMask,
                                 const UICaduceusFeedbackAnimator &feedback,
                                 uint32_t nowMs)
{
    HAL_Sprite_Clear();
    if (image.valid())
    {
        HAL_Sprite_PushImage(0, 0, image.width, image.height,
                             const_cast<uint16_t *>(image.pixels));
    }
    DrawProgressNumbers(progressMask);
    feedback.draw(nowMs);
}

void UICaduceus::DrawImageOnlyFrame(const SysRgb565View &image,
                                    const UICaduceusFeedbackAnimator &feedback,
                                    uint32_t nowMs)
{
    HAL_Sprite_Clear();
    if (image.valid())
    {
        HAL_Sprite_PushImage(0, 0, image.width, image.height,
                             const_cast<uint16_t *>(image.pixels));
    }
    feedback.draw(nowMs);
}

void UICaduceus::DrawMissingImage(const char *message,
                                  uint16_t progressMask,
                                  const UICaduceusFeedbackAnimator &feedback,
                                  uint32_t nowMs,
                                  bool showProgress)
{
    HAL_Sprite_Clear();
    const char *safeMessage = message ? message : "";
    const int width = HAL_Get_Text_Width_Font(safeMessage, HAL_FONT_BODY);
    const int x = max(4, (HAL_Get_Screen_Width() - width) / 2);
    const int y = (HAL_Get_Screen_Height() - HAL_Get_Font_Line_Height(HAL_FONT_BODY)) / 2;
    HAL_Draw_Rect(8, 8, HAL_Get_Screen_Width() - 16, HAL_Get_Screen_Height() - 16, TFT_RED);
    HAL_Screen_ShowLine_Font(x, y, safeMessage, HAL_FONT_BODY, TFT_RED);
    if (showProgress)
        DrawProgressNumbers(progressMask);
    feedback.draw(nowMs);
}
