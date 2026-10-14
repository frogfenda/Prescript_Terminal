/*
【测试职责】直接验证固件共用阻尼函数的帧间隔一致性、连续反向、循环坐标和静止收敛。
【边界】不模拟字体、屏幕 TE、DMA 和真实帧耗时；这些仍需要实机观察。
*/
#include "ui/ui_menu_motion.h"
#include "ui/ui_menu_ropes.h"
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

static void CheckRopes()
{
    UIMenuRopes ropes;
    UIMenuRopeAnchor anchors[9], shifted[9];
    for (int i = 0; i < 9; ++i)
    {
        const float y = (i - 4) * 32.0f;
        anchors[i] = {220.0f + y*y*0.003f, y, (float)(i - 4)};
        shifted[i] = {anchors[i].x, y, anchors[i].material + 195.0f};
    }
    // 交叉处前后股交替；循环坐标即使平移奇数槽，形状与编绳相位也不能突然跳变。
    for (float u = 1.0f; u <= 7.0f; u += 0.125f)
    {
        for (int strand = 0; strand < 2; ++strand)
        {
            const auto a = ropes.sample(anchors, 9, u, strand);
            const auto b = ropes.sample(shifted, 9, u, strand);
            assert(isfinite(a.x) && isfinite(a.y) && isfinite(a.depth));
            assert(fabsf(a.x-b.x) < 0.0001f && fabsf(a.y-b.y) < 0.0001f);
            assert(fabsf(a.depth-b.depth) < 0.0001f);
        }
        assert(fabsf(ropes.sample(anchors, 9, u, 0).depth +
                     ropes.sample(anchors, 9, u, 1).depth) < 0.00001f);
    }
    assert(ropes.sample(anchors, 9, 4.25f, 0).depth > 0.9f);
    assert(ropes.sample(anchors, 9, 4.75f, 0).depth < -0.9f);
    const auto idle_before = ropes.sample(anchors, 9, 4.0f, 0);
    for (int i = 0; i < 30; ++i) ropes.update(0.0f, 0.016f);
    const auto idle_after = ropes.sample(anchors, 9, 4.0f, 0);
    assert(fabsf(idle_after.x-idle_before.x) > 0.02f);
    assert(fabsf(idle_after.x-idle_before.x) < 3.0f);
    // 高速滚动与反向时仍有有限松弛量；停止后收回滞后，但闲置动画继续。
    for (int i = 0; i < 60; ++i) ropes.update(100.0f, 0.016f);
    assert(ropes.followOffset() > 0.19f && ropes.followOffset() <= 0.201f);
    const float before_reverse = ropes.followOffset();
    ropes.update(-100.0f, 0.016f);
    assert(fabsf(ropes.followOffset()-before_reverse) < 0.03f);
    for (int i = 0; i < 60; ++i) ropes.update(-100.0f, 0.016f);
    assert(ropes.followOffset() < -0.19f && ropes.followOffset() >= -0.201f);
    for (int i = 0; i < 120; ++i) ropes.update(0.0f, 0.016f);
    assert(ropes.followOffset() == 0.0f);
    ropes.clearMomentum();
    assert(ropes.followOffset() == 0.0f);
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
    CheckRopes();
    puts("PASS: menu motion cadence, reversal, wrap, layout convergence and rope continuity");
}
