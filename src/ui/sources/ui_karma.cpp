/*
【模块职责】实现业力图片的最近邻缩放和RGB565亮度调制。
【性能边界】最大画布96×96，每帧只处理一张图片；不申请临时内存，避免呼吸动画产生堆碎片。
*/
#include "ui/ui_karma.h"

#include <string.h>

namespace
{
    float ClampFloat(float value, float low, float high)
    {
        if (value < low) return low;
        if (value > high) return high;
        return value;
    }

    uint16_t ScaleColor565(uint16_t color, float brightness)
    {
        if (color == 0x0000)
            return 0x0000;

        const uint16_t r = (uint16_t)(((color >> 11) & 0x1F) * brightness);
        const uint16_t g = (uint16_t)(((color >> 5) & 0x3F) * brightness);
        const uint16_t b = (uint16_t)((color & 0x1F) * brightness);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }
}

bool UIKarma::RenderBreathingImage(uint16_t *buffer,
                                   uint16_t canvasSide,
                                   const SysRgb565View &image,
                                   float scale,
                                   float brightness)
{
    if (!buffer || canvasSide == 0)
        return false;

    memset(buffer, 0, (size_t)canvasSide * canvasSide * sizeof(uint16_t));
    if (!image.valid() || image.width != image.height)
        return false;

    scale = ClampFloat(scale, 0.1f, 1.0f);
    brightness = ClampFloat(brightness, 0.0f, 1.0f);
    uint16_t drawSide = (uint16_t)(canvasSide * scale);
    if (drawSide == 0) drawSide = 1;
    if (drawSide > canvasSide) drawSide = canvasSide;
    const uint16_t offset = (canvasSide - drawSide) / 2;

    for (uint16_t y = 0; y < drawSide; ++y)
    {
        const uint16_t srcY = (uint16_t)((uint32_t)y * image.height / drawSide);
        for (uint16_t x = 0; x < drawSide; ++x)
        {
            const uint16_t srcX = (uint16_t)((uint32_t)x * image.width / drawSide);
            const uint16_t color = image.pixels[(size_t)srcY * image.width + srcX];
            buffer[(size_t)(offset + y) * canvasSide + offset + x] =
                ScaleColor565(color, brightness);
        }
    }
    return true;
}
