/*
【模块职责】编辑滚轮的纯几何计算；实际渲染与宿主间距测试共用，不依赖 Arduino 或字体。
【间距约束】横向映射的最小导数为 0.72，按最宽选项计算节距后，任何小数滚动位置均保留字间距。
*/
#pragma once
#include <math.h>

namespace UIDialGeometry {
constexpr float MaxSlots = 3.65f;
constexpr float MinimumSlope = 0.72f;
inline float Pitch(float maximum_width, float screen_width)
{
    const float gap = fmaxf(12.0f, screen_width * 0.033f);
    return fmaxf(screen_width * 0.1215f, (maximum_width + gap) / MinimumSlope);
}
inline float X(float slot, float pitch)
{
    // 保留边缘压缩，但限制压缩程度，避免四位年份或中文标签在边缘互相压住。
    return pitch * (0.86f * slot + 0.14f * sinf(slot));
}
inline float Depth(float x, float half_span)
{
    const float t = fminf(1.0f, fabsf(x) / fmaxf(1.0f, half_span));
    return t * t;
}
inline float Brightness(float slot, float x, float half_span, float half_width)
{
    // 到达边界前用整个标签的外沿淡出，不在最后一帧突然丢掉一个完整数字。
    const float edge = fmaxf(0.0f, fminf(1.0f,
        (half_span - fabsf(x) - half_width) / 20.0f));
    const float slot_edge = fmaxf(0.0f, fminf(1.0f, (MaxSlots - fabsf(slot)) / 0.5f));
    return edge * slot_edge / (1.0f + 0.45f * slot * slot);
}
}
