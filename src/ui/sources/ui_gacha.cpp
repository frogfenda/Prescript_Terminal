/*
【待抽状态】散落碎镜无色轻晃，或车内视角街景持续行驶；下一次短按才抽取结果，运动时钟不中断。
【碎镜时间轴】0~80ms 无色正面；80~1480ms 同一道斜向反光扫遍屏幕并留下稀有度色；
1480~1990ms 保持，1990~2430ms 十块碎镜一起沿随机轴侧翻，露出 App 已画好的真实结算页。
【车窗时间轴】60~510ms 倒影渐显，220~620ms 开裂，730~1730ms 扫光着色，
1950~2300ms 镜面放大，2300~2800ms 翻面露出同一个真实结算页。倒影不含人物。
【几何策略】Start 生成十个 Voronoi 分区、内缩黑缝并随机斜切/旋转；Draw 始终裁在原分区内，
因此碎镜分布不依赖星级，转动不越界互盖。固定小数组缓存，没有逐帧随机、文件读取或整屏副本。
*/
#include "ui/ui_gacha.h"
#include "hal/hal.h"
#include "hal/hal_display_config.h"
#include <math.h>
#include <initializer_list>

namespace
{
    using UIGacha::Detail::Point;
    using UIGacha::Detail::Polygon;
    using UIGacha::Detail::Shard;
    using UIGacha::PullAppearance;
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
    void Fill(const Polygon &p, uint16_t color, int width, int height, uint8_t alpha=255)
    {
        if (p.count<3 || !alpha) return;
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
                if (right>=left)
                {
                    if (alpha==255) HAL_Fill_Rect(left,y,right-left+1,1,color);
                    else HAL_Blend_Rect(left,y,right-left+1,1,color,alpha);
                }
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

    // 车窗素材全部由 RGB565 图元生成。所有设计坐标统一映射到调用方画布；mask 在设计坐标中
    // 裁切倒影和扫光，最后才缩放，防止灯带/城市细节越出正在翻转的镜片。
    struct BusPainter
    {
        int width, height;
        const Polygon *mask=nullptr;
        float opacity=1;
        BusPainter(int w,int h) : width(w),height(h) {}

        struct RasterPolygon
        {
            int16_t x[UIGacha::Detail::MAX_VERTICES],y[UIGacha::Detail::MAX_VERTICES];
            int count,top,bottom;
            RasterPolygon(const Polygon &p,int width,int height) : count(p.count),top(height),bottom(-1)
            {
                for (int i=0;i<count;++i)
                {
                    x[i]=(int16_t)lroundf(p.points[i].x*width/428.0f);
                    y[i]=(int16_t)lroundf(p.points[i].y*height/142.0f);
                    top=min(top,(int)y[i]); bottom=max(bottom,(int)y[i]);
                }
            }
            bool Span(int row,float &left,float &right) const
            {
                left=1e9f; right=-1e9f;
                for (int i=0;i<count;++i)
                {
                    const int j=(i+1)%count;
                    if ((y[i]<=row&&y[j]>row)||(y[j]<=row&&y[i]>row))
                    {
                        const float point=x[i]+(float)(row-y[i])*(x[j]-x[i])/(y[j]-y[i]);
                        left=min(left,point); right=max(right,point);
                    }
                }
                return right>=left;
            }
        };

        void Paint(const Polygon &p, uint16_t color) const
        {
            if (opacity<=0 || p.count<3 || (mask&&mask->count<3)) return;
            // 车窗图元/镜片均为凸多边形：逐行求图元与遮罩的公共跨度即可，不必构造多次
            // 64 顶点临时交集。这既保留 RGB565 整数光栅，也缩短嵌套调用的主循环栈链。
            const RasterPolygon shape(p,width,height),boundary(mask?*mask:p,width,height);
            const uint8_t alpha=(uint8_t)lroundf(Clamp(opacity)*255);
            for (int row=max(0,max(shape.top,boundary.top));row<=min(height-1,min(shape.bottom,boundary.bottom));++row)
            {
                float left,right,clipLeft,clipRight;
                if (!shape.Span(row,left,right)||!boundary.Span(row,clipLeft,clipRight)) continue;
                const int x0=max(0,(int)ceilf(max(left,clipLeft))),x1=min(width-1,(int)floorf(min(right,clipRight)));
                if (x1<x0) continue;
                if (alpha==255) HAL_Fill_Rect(x0,row,x1-x0+1,1,color);
                else HAL_Blend_Rect(x0,row,x1-x0+1,1,color,alpha);
            }
        }
        void Poly(std::initializer_list<Point> points, uint16_t color) const
        {
            Polygon p;
            for (Point v:points) p.points[p.count++]=v;
            Paint(p,color);
        }
        void Box(float x,float y,float w,float h,uint16_t color) const
        { if (w>0&&h>0) Paint(Rect(x,y,x+w,y+h),color); }
        void Line(float x,float y,float xx,float yy,uint16_t color,float thickness=1) const
        {
            const float length=hypotf(xx-x,yy-y);
            if (length<.001f) { Box(x,y,thickness,thickness,color); return; }
            const float nx=-(yy-y)/length*thickness*.5f,ny=(xx-x)/length*thickness*.5f;
            Poly({{x+nx,y+ny},{xx+nx,yy+ny},{xx-nx,yy-ny},{x-nx,y-ny}},color);
        }

        void Scene(double seconds) const
        {
            Box(0,0,428,142,RGB(5,17,29)); Box(0,73,428,60,RGB(11,31,39));
            // 建筑编号绑定世界坐标；只由局部种子决定，不在重绘/按键时重新抽建筑或消耗业务随机数。
            for (int near=0;near<2;++near)
            {
                const int unit=near?59:81;
                const double travel=seconds*(near?91:24);
                const uint32_t base=(uint32_t)floor(travel/unit);
                for (int i=-1;i<10;++i)
                {
                    uint32_t seed=(base+i+40000U)*371U+near*97U;
                    const float x=i*unit-(float)fmod(travel,unit);
                    const float h=near?28+Next(seed)*46:48+Next(seed)*38,y=114-h;
                    const float w=unit-6-Next(seed)*9;
                    const uint16_t wall=near?RGB(19,43,52):RGB(12,29,41);
                    Box(x,y,w,h,wall);
                    if (Next(seed)>.5f)
                    {
                        Box(x+w*.2f,y-5,w*.5f,5,wall);
                        Line(x+w*.4f,y-5,x+w*.4f,y-14,RGB(21,43,51));
                    }
                    for (int row=0;row<(int)((h-8)/12);++row)
                        for (int col=0;col<(int)((w-8)/11);++col)
                            if (Next(seed)>.42f)
                                Box(x+6+col*11,y+8+row*12,3,5,near?
                                    (Next(seed)>.82f?RGB(156,112,55):RGB(50,105,114)):RGB(29,66,80));
                    if (near) Line(x,y,x+w,y,RGB(44,67,73));
                }
            }
            Line(0,117,428,117,RGB(43,64,68),2);
            for (int i=-1;i<12;++i)
            {
                const float x=i*49-(float)fmod(seconds*170,49);
                Box(x,118,4,13,RGB(10,25,33)); Box(x+9,121,23,2,RGB(74,66,44));
            }
            // 有顶梁、接缝、底座和斜纹的桥墩，与街景同向经过，不作为凭空出现的转场条。
            const float x=620-(float)fmod(seconds*215,1150);
            if (x < -95 || x > 500) return;
            Poly({{x-24,0},{x+83,0},{x+68,20},{x-9,20}},RGB(57,62,61));
            Box(x,20,55,110,RGB(47,52,53)); Box(x+40,20,15,110,RGB(26,36,42));
            Line(x+3,52,x+37,52,RGB(81,83,77)); Line(x+3,87,x+37,87,RGB(81,83,77));
            Poly({{x-8,116},{x+64,116},{x+73,142},{x-17,142}},RGB(57,59,53));
            for (int i=0;i<3;++i)
                Poly({{x+4+i*13,94},{x+12+i*13,94},{x+3+i*13,114},{x-5+i*13,114}},RGB(155,129,61));
        }

        void Interior() const
        {
            // 固定近处窗框、座椅边、厚窗台、锁扣和顶灯，明确“人在车内朝窗外看”的视角；不画人物。
            Poly({{85,16},{113,15},{173,61},{155,62}},RGB(29,49,56));
            Poly({{251,13},{266,13},{310,45},{299,46}},RGB(24,39,48));
            Poly({{0,0},{428,0},{428,9},{33,21},{0,29}},RGB(45,29,32));
            Poly({{0,0},{30,0},{39,119},{0,142}},RGB(54,31,35));
            Poly({{411,0},{428,0},{428,142},{410,107}},RGB(37,25,31));
            Poly({{27,112},{412,103},{428,124},{15,137}},RGB(79,48,44));
            Poly({{15,137},{428,124},{428,142},{0,142}},RGB(32,21,27));
            Line(34,20,410,9,RGB(16,18,23),3); Line(35,23,409,12,RGB(109,76,65));
            Line(38,112,410,103,RGB(15,22,28),3); Line(45,117,411,108,RGB(143,101,74),2);
            Line(35,23,38,111,RGB(19,22,27),3); Line(410,12,410,102,RGB(18,18,24),2);
            Box(61,3,83,5,RGB(93,63,43)); Box(66,4,72,2,RGB(179,140,86));
            Box(395,82,9,19,RGB(30,32,37)); Box(397,84,3,12,RGB(134,123,103));
            Poly({{0,76},{12,73},{31,79},{48,101},{52,142},{0,142}},RGB(76,29,38));
            Poly({{0,81},{11,78},{26,84},{39,106},{42,142},{0,142}},RGB(42,20,28));
            Line(13,81,28,88,RGB(117,49,53),2); Line(28,88,39,108,RGB(117,49,53),2);
            Poly({{31,124},{62,120},{79,126},{78,133},{39,137}},RGB(58,35,38));
            Line(39,126,63,123,RGB(115,76,64),2);
        }

        void Reflection(float t, double world, PullAppearance appearance, bool colored, int index) const
        {
            const Palette palette=Colors(appearance,colored,.5f);
            const uint16_t base=colored?palette.base:RGB(41,46,49);
            const uint16_t tint=colored?palette.tint:RGB(116,128,130);
            Box(0,0,428,142,base);
            const float drift=sinf((float)fmod(world*.7,6.283185307)+index*.4f)*3;
            Poly({{70+drift,14},{192+drift,17},{307+drift,103},{268+drift,107}},tint);
            Poly({{122+drift,19},{152+drift,20},{273+drift,107},{258+drift,107}},
                 colored?palette.bright:RGB(186,194,194));
            Poly({{292,106},{305,65},{320,58},{343,62},{369,112}},colored?base:RGB(24,30,34));
            Line(315,65,340,68,colored?palette.tint:RGB(84,92,94),2);
            Line(63,99,387,91,colored?palette.tint:RGB(77,88,90),2);
            if (t<=.65f) return;
            BusPainter city=*this;
            city.opacity*=Smooth((t-.65f)/.45f)*.7f;
            const float horizon=72+sinf(index*2.3f)*14;
            for (int j=0;j<9;++j)
            {
                const int x=j*57-20+(index*19)%38,h=13+(j*17+index*11)%35;
                city.Box(x,horizon-h,43,h,colored?base:RGB(24,34,39));
                for (int k=0;k<3;++k)
                    city.Box(x+6+k*10,horizon-h+7,3,5,colored?palette.tint:RGB(96,110,113));
            }
        }
    };

    Point GlassPoint(Point p, float zoom)
    {
        const float u=p.x/428,v=p.y/142;
        return {Mix(Mix(Mix(34,410,u),Mix(38,410,u),v),p.x,zoom),
                Mix(Mix(Mix(22,11,u),Mix(113,104,u),v),p.y,zoom)};
    }
    Polygon GlassPolygon(Polygon p,float zoom)
    { for (int i=0;i<p.count;++i) p.points[i]=GlassPoint(p.points[i],zoom); return p; }
}

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
        s.origin=Center(cell); s.axis=Next(seed)*kPi;
        // 车窗开裂阶段需要完整公共边，内缩到逐帧绘制时才进行；不对裂纹额外斜切。
        if (style_ == AnimationStyle::BusWindow)
        { s.phase=Next(seed)*6.28f; s.appearance=pulls[i]; continue; }
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
    DrawFrame(0, motion_ms, true);
}

void UIGacha::ExtractionAnimation::Draw(uint32_t elapsed_ms, uint32_t motion_ms) const
{
    if (!width_||elapsed_ms>=DurationMs()) return;
    if (style_ == AnimationStyle::BusWindow) { DrawBusFrame(elapsed_ms,motion_ms,false); return; }
    DrawFrame(elapsed_ms, motion_ms, false);
}

void UIGacha::ExtractionAnimation::DrawFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const
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

void UIGacha::ExtractionAnimation::DrawBusFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const
{
    // 演出时钟与行驶时钟分离：待抽多久都持续行驶，SetPulls/开始/掉帧均不重置世界坐标。
    // double 只用于长时间累计位移，避免待机数小时后 float 毫秒时间丢失短帧精度。
    const double world=motion_ms*.001;
    const float t=idle?-1:elapsed_ms*.001f;
    const float crack=Smooth((t-.22f)/.40f),zoom=Smooth((t-1.95f)/.35f);
    const float flip=Smooth((t-2.3f)/.5f),beam=Mix(-25,500,Clamp((t-.73f)/1.0f));
    BusPainter painter{width_,height_};
    const Polygon glass=GlassPolygon(Rect(0,0,428,142),0);
    if (idle || elapsed_ms<BUS_FLIP_START_MS)
    {
        painter.Scene(world);
        if (!idle)
        {
            painter.mask=&glass;
            painter.opacity=Smooth((t-.06f)/.45f);
            painter.Box(0,0,428,142,RGB(4,9,13));
            if (t>=.68f)
            {
                painter.opacity=Smooth((t-.62f)/.09f);
                painter.Box(0,0,428,142,RGB(3,8,11));
            }
            painter.mask=nullptr; painter.opacity=zoom;
            painter.Box(0,0,428,142,TFT_BLACK);
        }
        painter.opacity=1-zoom; painter.Interior(); painter.opacity=1;
    }
    if (idle) return;
    if (t<.68f)
    {
        painter.mask=&glass; painter.opacity=Smooth((t-.06f)/.45f);
        painter.Reflection(t,world,{},false,0);
        painter.opacity=1;
    }
    for (int index=0;index<PULL_COUNT;++index)
    {
        const Shard &shard=shards_[index];
        // Start 缓存的分区属于实际画布，先还原设计坐标，再映射到透视车窗/放大后的镜面。
        Polygon cell=shard.cell;
        for (int j=0;j<cell.count;++j)
        { cell.points[j].x*=428.0f/width_; cell.points[j].y*=142.0f/height_; }
        if (t<.68f)
        {
            if (crack<=0) continue;
            painter.mask=&glass;
            for (int j=0;j<cell.count;++j)
            {
                const Point a=GlassPoint(cell.points[j],0),b=GlassPoint(cell.points[(j+1)%cell.count],0);
                painter.Line(a.x,a.y,Mix(a.x,b.x,crack),Mix(a.y,b.y,crack),RGB(164,179,180));
            }
            continue;
        }
        Polygon p=Within(cell,cell,2.3f*crack);
        const Point origin={shard.origin.x*428/width_,shard.origin.y*142/height_};
        const float ax=cosf(shard.axis),ay=sinf(shard.axis),cs=cosf(flip*kPi*.5f);
        for (int j=0;j<p.count;++j)
        {
            const float dx=p.points[j].x-origin.x,dy=p.points[j].y-origin.y;
            const float u=dx*ax+dy*ay,v=-dx*ay+dy*ax;
            p.points[j]={origin.x+u*cs*ax-v*ay,origin.y+u*cs*ay+v*ax};
        }
        p=GlassPolygon(Within(p,cell,0),zoom);
        if (Area(p)<.15f) continue;
        painter.mask=&p;
        painter.Reflection(t,world,shard.appearance,false,index);
        const Polygon colored=Clip(p,1,.3f,beam);
        painter.mask=&colored;
        painter.Reflection(t,world,shard.appearance,true,index);
        painter.mask=nullptr;
        const Palette colors=Colors(shard.appearance,true,.5f);
        for (int j=0;j<p.count;++j)
        {
            Point a=p.points[j],b=p.points[(j+1)%p.count];
            painter.Line(a.x,a.y,b.x,b.y,RGB(166,184,185));
            const float da=a.x+.3f*a.y-beam,db=b.x+.3f*b.y-beam;
            if (da>0&&db>0) continue;
            if ((da<0&&db>0)||(da>0&&db<0))
            {
                const Point split={Mix(a.x,b.x,da/(da-db)),Mix(a.y,b.y,da/(da-db))};
                if (da>0) a=split; else b=split;
            }
            painter.Line(a.x,a.y,b.x,b.y,colors.tint);
        }
        if (t>.73f&&t<1.73f)
        {
            painter.Paint(Band(p,1,.3f,beam-4,beam+7),RGB(225,233,233));
            painter.Paint(Band(p,1,.3f,beam-1,beam+1),TFT_WHITE);
        }
    }
    // 镜面放大前车内结构始终压在玻璃上，避免倒影与裂纹穿过座椅/锁扣。
    if (t<1.95f)
    { painter.mask=nullptr; painter.opacity=1; painter.Interior(); }
}
