/*
【待抽状态】无色碎镜正面轻晃，直到 App 的下一次短按才抽取结果；晃动时钟延续到演出。
【演出时间轴】0~450ms 无色正面；450~1850ms 同一道斜向反光扫遍屏幕并留下稀有度色；
1850~2360ms 保持，2360~2800ms 十块碎镜一起沿随机轴侧翻，露出 App 已画好的真实结算页。
【几何策略】Start 生成十个 Voronoi 分区、内缩黑缝并随机斜切/旋转；Draw 始终裁在原分区内，
因此碎镜分布不依赖星级，转动不越界互盖。固定小数组缓存，没有逐帧随机、文件读取或整屏副本。
*/
#include "ui/ui_gacha.h"
#include "hal/hal.h"
#include "hal/hal_display_config.h"
#include <math.h>

namespace
{
    using UIGacha::Detail::Point;
    using UIGacha::Detail::Polygon;
    using UIGacha::Detail::Shard;
    constexpr float kPi = 3.14159265f;
    float Clamp(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
    float Smooth(float t) { t = Clamp(t); return t*t*(3-2*t); }
    float Mix(float a, float b, float t) { return a+(b-a)*t; }
    uint16_t RGB(int r, int g, int b)
    {
        return (uint16_t)(((int)lroundf(r*31.0f/255)<<11) |
                          ((int)lroundf(g*63.0f/255)<<5) | (int)lroundf(b*31.0f/255));
    }

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

    // 扫描线只提交整数 HAL_Fill_Rect，不依赖浏览器抗锯齿或宿主三角形填充算法。
    void Fill(const Polygon &p, uint16_t color, int width, int height)
    {
        if (p.count<3) return;
        int xs[UIGacha::Detail::MAX_VERTICES],ys[UIGacha::Detail::MAX_VERTICES];
        int top=height,bottom=-1;
        for (int i=0;i<p.count;++i)
        {
            xs[i]=(int)lroundf(p.points[i].x); ys[i]=(int)lroundf(p.points[i].y);
            top=min(top,ys[i]); bottom=max(bottom,ys[i]);
        }
        for (int y=max(0,top);y<=min(height-1,bottom);++y)
        {
            float intersections[UIGacha::Detail::MAX_VERTICES]; int count=0;
            for (int i=0;i<p.count;++i)
            {
                const int j=(i+1)%p.count;
                if ((ys[i]<=y&&ys[j]>y)||(ys[j]<=y&&ys[i]>y))
                {
                    const float x=xs[i]+(float)(y-ys[i])*(xs[j]-xs[i])/(ys[j]-ys[i]);
                    int k=count++;
                    while (k>0&&intersections[k-1]>x) { intersections[k]=intersections[k-1]; --k; }
                    intersections[k]=x;
                }
            }
            for (int i=0;i+1<count;i+=2)
            {
                const int left=max(0,(int)ceilf(intersections[i]));
                const int right=min(width-1,(int)floorf(intersections[i+1]));
                if (right>=left) HAL_Fill_Rect(left,y,right-left+1,1,color);
            }
        }
    }

    void Edge(Point a, Point b, uint16_t color)
    {
        HAL_Draw_Line((int)lroundf(a.x),(int)lroundf(a.y),(int)lroundf(b.x),(int)lroundf(b.y),color);
    }

    Polygon Band(const Polygon &p, float a, float b, float start, float end)
    {
        return Clip(Clip(p,a,b,end),-a,-b,-start);
    }

    struct Palette { uint16_t base,dark,mid,tint,bright; };
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

    // 与无色面使用同一套倒影几何；只改变调色板，因此扫光边界不会使倒影跳动。
    void Surface(const Polygon &p, const Shard &s, float slope, float offset, float horizon,
                 const Palette &c, int width, int height)
    {
        Fill(p,c.base,width,height);
        Fill(Band(p,1,slope,offset-17,offset+14),c.tint,width,height);
        Fill(Band(p,1,slope,offset+8,offset+14),c.bright,width,height);
        Fill(Band(p,0,1,horizon,horizon+9),c.dark,width,height);
        for (int j=0;j<2;++j)
        {
            Polygon shadow=Band(p,1,0,s.origin.x-22+j*24,s.origin.x-11+j*24);
            Fill(Band(shadow,0,1,horizon-14-j*7,horizon+8),c.mid,width,height);
        }
    }

    Polygon Project(const Shard &s, float t, float angle)
    {
        Polygon p=s.cell;
        const float ax=cosf(s.axis),ay=sinf(s.axis),cs=cosf(angle),sn=sinf(angle);
        const float tilt=sinf(t*2+s.phase)*.014f,ct=cosf(tilt),st=sinf(tilt);
        for (int i=0;i<p.count;++i)
        {
            const float x=p.points[i].x-s.origin.x,y=p.points[i].y-s.origin.y;
            const float along=x*ax+y*ay,across=-x*ay+y*ax;
            const float perspective=1/(1+sn*along*.0014f);
            const float u=along*cs*perspective,v=across*perspective;
            p.points[i]={s.origin.x+(u*ax-v*ay)*ct-(u*ay+v*ax)*st,
                         s.origin.y+(u*ax-v*ay)*st+(u*ay+v*ax)*ct};
        }
        return Within(p,s.cell,0);
    }
}

void UIGacha::MirrorAnimation::Start(uint32_t seed, const PullAppearance (&pulls)[PULL_COUNT], int width, int height)
{
    width_=width; height_=height;
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
            Polygon p=Rect(5*sx,6*sy,width-6*sx,height-7*sy);
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
        s.origin=Center(cell); s.axis=Next(seed)*kPi;
        Polygon inset=Within(cell,cell,2.2f*min(sx,sy));
        // 额外削掉一个随机角并做小幅平面旋转，避免形状呈现规则瓷砖或卡片阵列。
        if (inset.count>3)
        {
            const int index=min(inset.count-1,(int)(Next(seed)*inset.count));
            const Point p=inset.points[index],prev=inset.points[(index+inset.count-1)%inset.count];
            const Point next=inset.points[(index+1)%inset.count];
            const float cut=.15f+Next(seed)*.20f;
            const Point a={Mix(p.x,prev.x,cut),Mix(p.y,prev.y,cut)};
            const Point b={Mix(p.x,next.x,cut),Mix(p.y,next.y,cut)};
            float nx=b.y-a.y,ny=a.x-b.x,d=nx*a.x+ny*a.y;
            if (nx*s.origin.x+ny*s.origin.y>d) { nx=-nx; ny=-ny; d=-d; }
            inset=Clip(inset,nx,ny,d);
        }
        const float scale=.91f+Next(seed)*.075f,tilt=(Next(seed)-.5f)*.48f;
        const float ct=cosf(tilt),st=sinf(tilt);
        for (int j=0;j<inset.count;++j)
        {
            const float x=inset.points[j].x-s.origin.x,y=inset.points[j].y-s.origin.y;
            inset.points[j]={s.origin.x+(x*ct-y*st)*scale,s.origin.y+(x*st+y*ct)*scale};
        }
        s.cell=Within(inset,cell,2.1f*min(sx,sy));
        s.phase=Next(seed)*6.28f;
        s.appearance=pulls[i];
    }
}

void UIGacha::MirrorAnimation::Draw(uint32_t elapsed_ms) const
{
    Draw(elapsed_ms, elapsed_ms);
}

void UIGacha::MirrorAnimation::SetPulls(const PullAppearance (&pulls)[PULL_COUNT])
{
    for (int i=0;i<PULL_COUNT;++i) shards_[i].appearance=pulls[i];
}

void UIGacha::MirrorAnimation::DrawIdle(uint32_t motion_ms) const
{
    DrawFrame(0, motion_ms, true);
}

void UIGacha::MirrorAnimation::Draw(uint32_t elapsed_ms, uint32_t motion_ms) const
{
    DrawFrame(elapsed_ms, motion_ms, false);
}

void UIGacha::MirrorAnimation::DrawFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const
{
    if (!width_||(!idle&&elapsed_ms>=DURATION_MS)) return;
    const float t=motion_ms*.001f;
    const float sweep=Mix(-38,width_+82,Clamp(((float)elapsed_ms-SWEEP_START_MS)/(SWEEP_END_MS-SWEEP_START_MS)));
    const float angle=kPi*.5f*Smooth(((float)elapsed_ms-FLIP_START_MS)/(DURATION_MS-FLIP_START_MS));
    for (const Shard &s:shards_)
    {
        const Polygon p=Project(s,t,angle);
        if (p.count<3||Area(p)<.1f) continue;
        const float light=.5f+.5f*sinf(t*2.9f+s.phase);
        const float slope=-.35f+sinf(s.axis)*.75f;
        const float offset=s.origin.x+s.origin.y*slope+sinf(t*2+s.phase)*23;
        const float horizon=s.origin.y+sinf(t*1.4f+s.phase)*7;
        const Palette neutral=Colors(s.appearance,false,light),colored=Colors(s.appearance,true,light);
        Surface(p,s,slope,offset,horizon,neutral,width_,height_);
        if (!idle) Surface(Clip(p,1,.32f,sweep),s,slope,offset,horizon,colored,width_,height_);
        for (int j=0;j<p.count;++j)
        {
            Point a=p.points[j],b=p.points[(j+1)%p.count];
            const bool lit=(b.x-a.x)*.35f-(b.y-a.y)>0;
            Edge({a.x+1,a.y+1},{b.x+1,b.y+1},neutral.dark);
            Edge(a,b,lit?neutral.bright:neutral.mid);
            if (idle) continue;
            // 边缘也按同一束光裁切，不能在某块镜片尚未被扫到时提前露出红/金/绿色。
            const float da=a.x+.32f*a.y-sweep,db=b.x+.32f*b.y-sweep;
            if (da>0&&db>0) continue;
            if ((da<0&&db>0)||(da>0&&db<0))
            {
                const Point split={Mix(a.x,b.x,da/(da-db)),Mix(a.y,b.y,da/(da-db))};
                if (da>0) a=split; else b=split;
            }
            Edge(a,b,lit?colored.tint:colored.mid);
        }
        if (!idle&&elapsed_ms>SWEEP_START_MS&&elapsed_ms<SWEEP_END_MS)
        {
            Fill(Band(p,1,.32f,sweep-6,sweep+9),RGB(220,220,220),width_,height_);
            Fill(Band(p,1,.32f,sweep-2,sweep+2),TFT_WHITE,width_,height_);
        }
    }
}
