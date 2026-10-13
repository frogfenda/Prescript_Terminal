/*
【测试职责】直接验证固件共用阻尼函数的帧间隔一致性、连续反向、循环坐标和静止收敛。
【边界】不模拟字体、屏幕 TE、DMA 和真实帧耗时；这些仍需要实机观察。
*/
#include "ui/ui_menu_motion.h"
#include <cassert>
#include <cstdio>

static float AdvanceFor(float dt, int frames)
{
    float position = 0.0f, velocity = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        UI_MenuAdvanceDamped(3.0f, dt, 24.0f, position, velocity);
        assert(isfinite(position) && isfinite(velocity));
        assert(position >= 0.0f && position <= 3.0f);
    }
    return position;
}

int main()
{
    // 相同实际时间、不同帧间隔必须产生相同位移；偶发 48ms 帧不能改变动画节奏。
    assert(fabsf(AdvanceFor(0.016f, 12) - AdvanceFor(0.032f, 6)) < 0.00002f);
    assert(fabsf(AdvanceFor(0.016f, 12) - AdvanceFor(0.048f, 4)) < 0.00002f);
    float position = 0.0f, velocity = 0.0f;
    for (int i = 0; i < 3; ++i)
        UI_MenuAdvanceDamped(8.0f, 0.016f, 24.0f, position, velocity);
    assert(velocity > 0.0f);
    const float before_reverse = position;
    UI_MenuAdvanceDamped(-2.0f, 0.016f, 24.0f, position, velocity);
    assert(fabsf(position - before_reverse) < 1.0f);
    for (int i = 0; i < 200; ++i)
        UI_MenuAdvanceDamped(-2.0f, 0.016f, 24.0f, position, velocity);
    assert(position == -2.0f && velocity == 0.0f);
    assert(!UI_MenuAdvanceDamped(-2.0f, 0.016f, 24.0f, position, velocity));

    // 坐标平移（跨循环边界）不会走错误的最短路或改变运动轨迹。
    float a = 1.0f, b = 129.0f, va = 0.0f, vb = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        UI_MenuAdvanceDamped(3.0f, 0.016f, 24.0f, a, va);
        UI_MenuAdvanceDamped(131.0f, 0.016f, 24.0f, b, vb);
        assert(fabsf((b - a) - 128.0f) < 0.00005f);
    }
    float height = 28.0f;
    for (int i = 0; i < 200; ++i)
    {
        UI_MenuApproach(50.0f, 0.016f, 22.0f, height);
        assert(height >= 28.0f && height <= 50.0f);
    }
    assert(height == 50.0f);
    assert(!UI_MenuApproach(50.0f, 0.016f, 22.0f, height));
    puts("PASS: menu motion cadence, reversal, wrap and layout convergence");
}
