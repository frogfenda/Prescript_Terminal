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

static void CheckRopeVariation()
{
    UIMenuRopeAnchor anchors[9];
    for (int i=0;i<9;++i) anchors[i]={(float)(i*65),38.0f,(i-4)*0.5f};
    UIMenuRopes a, same, different;
    a.setVariation(123U,1.0f); same.setVariation(123U,1.0f); different.setVariation(456U,1.0f);
    bool seed_changes_shape=false;
    float minimum_anchor_x=10000.0f, maximum_anchor_x=-10000.0f;
    float previous_anchor_x=0.0f;
    float previous_y=a.sample(anchors,9,3.35f,0,0.5f).y;
    // 超过时间噪声的 256 格回绕周期，验证长时间待机的连续性和波幅约束。
    for (int frame=0;frame<12000;++frame)
    {
        UIMenuRopeAnchor rail[9], repeat[9];
        a.horizontalAnchors(rail,9,19,409,41,3,0);
        same.horizontalAnchors(repeat,9,19,409,41,3,0);
        assert(rail[1].x==19 && rail[7].x==409);
        for(int k=0;k<9;++k) {
            assert(rail[k].x==repeat[k].x && rail[k].y==41);
            if(k) assert(rail[k].x-rail[k-1].x>=25.99f);
        }
        // 慢速漂移必须真实改变区间宽度，同时长时间/时间回绕不能跳帧或使样条向后折返。
        minimum_anchor_x=fminf(minimum_anchor_x,rail[3].x);
        maximum_anchor_x=fmaxf(maximum_anchor_x,rail[3].x);
        if(frame) assert(fabsf(rail[3].x-previous_anchor_x)<2.5f);
        previous_anchor_x=rail[3].x;
        float previous_x=19.0f;
        for(float u=1.125f;u<=7;u+=0.125f) {
            const float x=a.sample(rail,9,u,0).x;
            assert(x>previous_x); previous_x=x;
        }
        for (float u=1;u<=7;u+=0.5f)
            for (int strand=0;strand<2;++strand)
            {
                const auto p=a.sample(anchors,9,u,strand,0.5f);
                const auto repeated=a.sample(anchors,9,u,strand,0.5f);
                const auto q=same.sample(anchors,9,u,strand,0.5f);
                assert(p.x==repeated.x && p.y==repeated.y && p.depth==repeated.depth);
                assert(p.y==q.y && p.depth==q.depth);
                assert(fabsf(p.y-38.0f)<=2.901f);
                // 编辑导轨展开到 0.85 倍时仍保持在约 ±5px 内，不因随机扰动突破留白。
                const auto wider=a.sample(anchors,9,u,strand,0.85f);
                assert(fabsf(wider.y-38.0f)<=4.931f);
                const auto menu_scale=a.sample(anchors,9,u,strand);
                assert(fabsf(menu_scale.y-38.0f)<=5.801f);
                seed_changes_shape |= fabsf(p.y-different.sample(anchors,9,u,strand,0.5f).y)>0.1f;
            }
        a.update(0,0.1f); same.update(0,0.1f); different.update(0,0.1f);
        const float y=a.sample(anchors,9,3.35f,0,0.5f).y;
        assert(fabsf(y-previous_y)<0.5f);
        previous_y=y;
    }
    assert(seed_changes_shape);
    assert(maximum_anchor_x-minimum_anchor_x>15.0f);
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
    CheckRopeVariation();
    puts("PASS: menu motion, rope continuity, seeded variation, bounds and long idle wrap");
}
