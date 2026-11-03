/*
【模块职责】抽卡演出的统一入口、布局生成与公共凸多边形工具。
【文件分工】gacha_mirror.cpp 维护散落碎镜；gacha_bus.cpp 维护行驶车窗；
本文件只共享几何/配色与生命周期，不处理 App 输入、抽卡概率、配置或文件资源。
【约束】UI 主循环调用，固定容量布局，一次 Start 缓存后逐帧复用。
*/
#include "gacha_internal.h"
#include "hal/hal.h"
#include "hal/hal_display_config.h"

namespace UIGacha { namespace Detail
{
    float Clamp(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
    float Smooth(float t) { t = Clamp(t); return t*t*(3-2*t); }
    float Mix(float a, float b, float t) { return a+(b-a)*t; }
    // 与概念预览相同的局部种子生成器，几何重建与设备全局 random() 完全隔离。
    float Next(uint32_t &seed)
    {
        seed += 0x6D2B79F5U;
        uint32_t q = (seed^(seed>>15))*(seed|1U);
        q ^= q+((q^(q>>7))*(q|61U));
        return (float)((q^(q>>14)) / 4294967296.0);
    }

    Polygon Rect(float x0, float y0, float x1, float y1)
    {
        Polygon p;
        p.count=4;
        p.points[0]={x0,y0}; p.points[1]={x1,y0};
        p.points[2]={x1,y1}; p.points[3]={x0,y1};
        return p;
    }

    // 保留 ax+by<=d 的半平面；空/退化多边形自然传递，不给 HAL 提交无效跨度。
    Polygon Clip(const Polygon &p, float a, float b, float d)
    {
        Polygon out;
        const auto append=[&](Point point) {
            if (out.count < UIGacha::Detail::MAX_VERTICES) out.points[out.count++]=point;
        };
        for (int i=0;i<p.count;++i)
        {
            const Point u=p.points[i],v=p.points[(i+1)%p.count];
            const float pu=u.x*a+u.y*b-d,pv=v.x*a+v.y*b-d;
            if (pu<=0) append(u);
            if ((pu<0&&pv>0)||(pu>0&&pv<0))
            {
                const float f=pu/(pu-pv);
                append({Mix(u.x,v.x,f),Mix(u.y,v.y,f)});
            }
        }
        return out;
    }

    float Area(const Polygon &p)
    {
        float sum=0;
        for (int i=0;i<p.count;++i)
        {
            const Point a=p.points[i],b=p.points[(i+1)%p.count];
            sum+=a.x*b.y-b.x*a.y;
        }
        return fabsf(sum)*.5f;
    }

    Point Center(const Polygon &p)
    {
        float sum=0,x=0,y=0;
        for (int i=0;i<p.count;++i)
        {
            const Point a=p.points[i],b=p.points[(i+1)%p.count];
            const float cross=a.x*b.y-b.x*a.y;
            sum+=cross; x+=(a.x+b.x)*cross; y+=(a.y+b.y)*cross;
        }
        return fabsf(sum)<.0001f ? Point{0,0} : Point{x/(3*sum),y/(3*sum)};
    }

    Polygon Within(Polygon p, const Polygon &boundary, float inset)
    {
        for (int i=0;i<boundary.count;++i)
        {
            const Point a=boundary.points[i],b=boundary.points[(i+1)%boundary.count];
            const float nx=b.y-a.y,ny=a.x-b.x;
            p=Clip(p,nx,ny,nx*a.x+ny*a.y-inset*hypotf(nx,ny));
        }
        return p;
    }

    void Edge(Point a, Point b, uint16_t color)
    {
        HAL_Draw_Line((int)lroundf(a.x),(int)lroundf(a.y),(int)lroundf(b.x),(int)lroundf(b.y),color);
    }

    Palette Colors(UIGacha::PullAppearance appearance, bool colored, float light)
    {
        if (!colored)
        {
            const int base=44+(int)(light*18),silver=113+(int)(light*28);
            return {RGB(base,base,base),RGB(19,19,19),RGB(47,47,47),RGB(silver,silver,silver),RGB(190,190,190)};
        }
        if (appearance.walp)
            return {RGB(27,65,42),RGB(11,29,21),RGB(35,79,52),RGB(80,235,130),RGB(186,255,204)};
        if (appearance.star==3)
            return {RGB(80,60,24),RGB(34,26,12),RGB(88,65,25),RGB(255,205,61),RGB(255,239,173)};
        if (appearance.star==2)
            return {RGB(79,30,36),RGB(35,12,19),RGB(88,34,43),RGB(246,74,85),RGB(255,180,181)};
        return {RGB(49,61,66),RGB(18,26,30),RGB(40,54,60),RGB(198,220,224),RGB(231,239,239)};
    }

}}

using namespace UIGacha::Detail;

void UIGacha::ExtractionAnimation::Start(uint32_t seed, const PullAppearance (&pulls)[PULL_COUNT], int width, int height, AnimationStyle style)
{
    style_=style; width_=width; height_=height;
    if (width<32||height<24) { width_=0; return; }
    const float sx=width/(float)HAL::DisplayConfig::LOGICAL_WIDTH;
    const float sy=height/(float)HAL::DisplayConfig::LOGICAL_HEIGHT;
    // 候选点采用最小间距拒绝采样，最多尝试 32 组；固定上限避免病态种子阻塞主循环。
    for (int trial=0;trial<32;++trial)
    {
        Point sites[PULL_COUNT];
        sites[0]={(18+Next(seed)*392)*sx,(15+Next(seed)*112)*sy};
        for (int i=1;i<PULL_COUNT;++i)
        {
            float score=-1; Point pick={0,0};
            for (int k=0;k<130;++k)
            {
                Point p={(12+Next(seed)*404)*sx,(12+Next(seed)*118)*sy};
                float nearest=1e12f;
                for (int j=0;j<i;++j)
                {
                    const float dx=(sites[j].x-p.x)/sx,dy=(sites[j].y-p.y)/sy;
                    nearest=min(nearest,dx*dx+dy*dy);
                }
                if (nearest>score) { score=nearest; pick=p; }
                if (nearest>48*48) { pick=p; break; }
            }
            sites[i]=pick;
        }
        float smallest=1e12f,largest=0;
        for (int i=0;i<PULL_COUNT;++i)
        {
            Polygon p=style_ == AnimationStyle::BusWindow ? Rect(sx,sy,width-sx,height-sy)
                                                         : Rect(5*sx,6*sy,width-6*sx,height-7*sy);
            for (int j=0;j<PULL_COUNT;++j)
            {
                if (i==j) continue;
                const Point a=sites[i],b=sites[j];
                p=Clip(p,b.x-a.x,b.y-a.y,(b.x*b.x+b.y*b.y-a.x*a.x-a.y*a.y)/2);
            }
            shards_[i].cell=p;
            smallest=min(smallest,Area(p)/(sx*sy)); largest=max(largest,Area(p)/(sx*sy));
        }
        if (smallest>1500&&largest<11600) break;
    }
    for (int i=0;i<PULL_COUNT;++i)
    {
        Shard &s=shards_[i];
        const Polygon cell=s.cell;
        s.origin=Center(cell); s.axis=Next(seed)*kPi; s.appearance=pulls[i];
        // 车窗开裂阶段需要完整公共边，内缩到逐帧绘制时才进行；不对裂纹额外斜切。
        if (style_ == AnimationStyle::BusWindow)
        { s.phase=Next(seed)*6.28f; continue; }
        PrepareMirrorShard(s,seed,sx,sy);
    }
}

void UIGacha::ExtractionAnimation::Draw(uint32_t elapsed_ms) const
{
    Draw(elapsed_ms, elapsed_ms);
}

void UIGacha::ExtractionAnimation::SetPulls(const PullAppearance (&pulls)[PULL_COUNT])
{
    for (int i=0;i<PULL_COUNT;++i) shards_[i].appearance=pulls[i];
}

void UIGacha::ExtractionAnimation::DrawIdle(uint32_t motion_ms) const
{
    if (!width_) return;
    if (style_ == AnimationStyle::BusWindow) { DrawBusFrame(0,motion_ms,true); return; }
    DrawMirrorFrame(0, motion_ms, true);
}

void UIGacha::ExtractionAnimation::Draw(uint32_t elapsed_ms, uint32_t motion_ms) const
{
    if (!width_||elapsed_ms>=DurationMs()) return;
    if (style_ == AnimationStyle::BusWindow) { DrawBusFrame(elapsed_ms,motion_ms,false); return; }
    DrawMirrorFrame(elapsed_ms, motion_ms, false);
}
