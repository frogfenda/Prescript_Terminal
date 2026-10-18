/*
【模块职责】使用少量采样线段绘制双红绳，交叉处按深度交替遮挡。
【绘制边界】在调用方的背景层绘制，不画绳孔或结点；裁剪到指定水平范围，文字随后透明叠加。
【性能】固定每控制点间隔 12 段；九点以内共用 2336 字节静态采样区，三层绘制只采样一次。
【约束】采样区仅供主循环串行绘制复用，不可跨任务/重入调用；无堆分配，较多控制点保留逐层采样。
*/
#include "ui/ui_menu_ropes.h"
#include "hal/hal.h"

namespace
{
    uint16_t Red(float brightness, bool front)
    {
        const int r = (int)((front ? 31.0f : 20.0f) * brightness);
        const int g = (int)((front ? 6.0f : 1.0f) * brightness);
        const int b = (int)((front ? 4.0f : 2.0f) * brightness);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }
}

void UIMenuRopes::draw(const UIMenuRopeAnchor *anchors, int count, float visual_position,
                      int clip_left, int clip_right, float amplitude_scale) const
{
    if (!anchors || count < 4) return;
    constexpr int Steps = 12;
    constexpr int CachedSegments = 6;
    constexpr int CachedPoints = CachedSegments*Steps+1;
    // 当前主菜单和编辑导轨都是九控制点。共享临时区，不在每个页面常驻一份，也不占用任务栈。
    // 只缓存本次 draw 的曲线几何：种子、输入、遮挡顺序和每个像素的落点保持原算法。
    static UIMenuRopeSample samples[2][CachedPoints];
    const bool cached = count-3 <= CachedSegments;
    if (cached)
        for (int strand=0; strand<2; ++strand)
            for (int segment=1; segment<count-2; ++segment)
                for (int step=segment==1 ? 0 : 1; step<=Steps; ++step)
                    samples[strand][(segment-1)*Steps+step] =
                        sample(anchors,count,segment+step/(float)Steps,strand,amplitude_scale);
    const int height = HAL_Get_Screen_Height();
    auto pixel = [&](int x, int y, uint16_t color) {
        if (x < clip_left || x >= clip_right || y < 0 || y >= height) return;
        HAL_Draw_Pixel(x, y, color);
    };
    // 两端各留一个屏外控制点，只画中间的连续曲线，出入屏时不会显露绳头。
    // 分开画完整的前股侧缘与主线，避免沿横向采样时侧缘擦掉刚画出的红色像素。
    for (int pass = 0; pass < 3; ++pass)
        for (int strand = 0; strand < 2; ++strand)
            for (int segment = 1; segment < count - 2; ++segment)
            {
                const int first = (segment-1)*Steps;
                auto a = cached ? samples[strand][first] :
                    sample(anchors, count, (float)segment, strand, amplitude_scale);
                for (int step = 1; step <= Steps; ++step)
                {
                    const auto b = cached ? samples[strand][first+step] :
                        sample(anchors, count, segment + step / (float)Steps, strand, amplitude_scale);
                    const bool in_front = (a.depth + b.depth) >= 0.0f;
                    if (in_front == (pass != 0))
                    {
                        const float brightness = fmaxf(0.20f, 1.0f - fabsf((a.material + b.material)*0.5f - visual_position)*0.24f);
                        const uint16_t color = Red(brightness, pass != 0);
                        const float dx = b.x - a.x, dy = b.y - a.y;
                        const int pixels = max(1, (int)ceilf(fmaxf(fabsf(dx), fabsf(dy))));
                        for (int p = 0; p <= pixels; ++p)
                        {
                            const float t = (float)p / pixels;
                            const int x = (int)lroundf(a.x + dx*t), y = (int)lroundf(a.y + dy*t);
                            // 前股暗色侧缘遮住后股；此时尚未绘制文字，侧缘不会擦掉字形。
                            // 侧缘沿线段法线方向展开，横向导轨与纵向菜单使用同一套交叉遮挡。
                            if (pass == 1)
                            {
                                if (fabsf(dy) >= fabsf(dx)) { pixel(x-1,y,TFT_BLACK); pixel(x+1,y,TFT_BLACK); }
                                else { pixel(x,y-1,TFT_BLACK); pixel(x,y+1,TFT_BLACK); }
                            }
                            else pixel(x, y, color);
                        }
                    }
                    a = b;
                }
            }
}
