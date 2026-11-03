/*
【模块职责】散落碎镜的反光、稀有度扫光与随机轴翻面；只维护此效果的渲染。
【调用关系】ExtractionAnimation 统一分发，布局和真实结果摘要由 ui_gacha.cpp 缓存。
【约束】不推屏、不分配整屏缓冲、不执行抽卡/持久化；与车窗共用公共几何工具。
*/
#include "gacha_internal.h"
#include "hal/hal.h"
using namespace UIGacha::Detail;
namespace
{
    // 每片镜子仅扫描一次外轮廓。反光带、地平线与楼影都化为此行上的整数区间，
    // 不再为每个材质层构造/裁切新的 64 顶点多边形；扫光两侧互不重复覆盖。
    void SurfaceRow(int left,int right,int y,const Shard &s,float slope,float offset,float horizon,const Palette &c)
    {
        if (right<left) return;
        const auto span=[&](int a,int b,uint16_t color) {
            a=max(left,a); b=min(right,b);
            if (b>=a) HAL_Fill_Rect(a,y,b-a+1,1,color);
        };
        span(left,right,c.base);
        const float line=offset-y*slope;
        span((int)ceilf(line-17),(int)floorf(line+14),c.tint);
        span((int)ceilf(line+8),(int)floorf(line+14),c.bright);
        if (y>=(int)lroundf(horizon)&&y<(int)lroundf(horizon+9)) span(left,right,c.dark);
        for (int j=0;j<2;++j)
            if (y>=(int)lroundf(horizon-14-j*7)&&y<(int)lroundf(horizon+8))
                span((int)lroundf(s.origin.x-22+j*24),(int)lroundf(s.origin.x-11+j*24),c.mid);
    }
    void Surface(const Polygon &p,const Shard &s,float slope,float offset,float horizon,
                 const Palette &neutral,const Palette &colored,bool idle,float sweep,bool beam,int width,int height)
    {
        int xs[UIGacha::Detail::MAX_VERTICES],ys[UIGacha::Detail::MAX_VERTICES];
        int top=height,bottom=-1;
        for (int i=0;i<p.count;++i)
        {
            xs[i]=(int)lroundf(p.points[i].x); ys[i]=(int)lroundf(p.points[i].y);
            top=min(top,ys[i]); bottom=max(bottom,ys[i]);
        }
        for (int y=max(0,top);y<=min(height-1,bottom);++y)
        {
            float left=1e9f,right=-1e9f;
            for (int i=0;i<p.count;++i)
            {
                const int j=(i+1)%p.count;
                if ((ys[i]<=y&&ys[j]>y)||(ys[j]<=y&&ys[i]>y))
                {
                    const float x=xs[i]+(float)(y-ys[i])*(xs[j]-xs[i])/(ys[j]-ys[i]);
                    left=min(left,x); right=max(right,x);
                }
            }
            const int x0=max(0,(int)ceilf(left)),x1=min(width-1,(int)floorf(right));
            if (x1<x0) continue;
            const int boundary=idle?x0-1:(int)floorf(sweep-.32f*y);
            SurfaceRow(x0,min(x1,boundary),y,s,slope,offset,horizon,colored);
            SurfaceRow(max(x0,boundary+1),x1,y,s,slope,offset,horizon,neutral);
            if (beam)
            {
                const auto band=[&](float a,float b,uint16_t color) {
                    const int l=max(x0,(int)ceilf(sweep-.32f*y+a)),r=min(x1,(int)floorf(sweep-.32f*y+b));
                    if (r>=l) HAL_Fill_Rect(l,y,r-l+1,1,color);
                };
                band(-6,9,RGB(220,220,220)); band(-2,2,TFT_WHITE);
            }
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

// 布局斜切/旋转也归镜面样式维护；公共入口只负责 Voronoi 分区和局部随机种子。
void UIGacha::Detail::PrepareMirrorShard(Shard &s,uint32_t &seed,float sx,float sy)
{
    const Polygon cell=s.cell;
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
}

void UIGacha::ExtractionAnimation::DrawMirrorFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const
{
    if (!width_||(!idle&&elapsed_ms>=DurationMs())) return;
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
        Surface(p,s,slope,offset,horizon,neutral,colored,idle,sweep,
                !idle&&elapsed_ms>SWEEP_START_MS&&elapsed_ms<SWEEP_END_MS,width_,height_);
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

    }
}

