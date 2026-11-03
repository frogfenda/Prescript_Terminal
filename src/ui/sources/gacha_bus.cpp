/*
【模块职责】持续行驶的车内视角、倒影裂纹、十面镜世界扫光与翻面。
【调用关系】ExtractionAnimation 传入两条绝对时间轴：提取进度与连续行驶时间。
【约束】不含人物、文件读取或逐帧业务随机；图元只写现有 Sprite，最终结算仍由 App 绘制。
*/
#include "gacha_internal.h"
#include "hal/hal.h"
#include "hal/hal_display_config.h"
#include <initializer_list>
using namespace UIGacha::Detail;
using UIGacha::PullAppearance;
namespace
{
    // 车窗素材全部由 RGB565 图元生成。所有设计坐标统一映射到调用方画布；mask 在设计坐标中
    // 裁切倒影和扫光，最后才缩放，防止灯带/城市细节越出正在翻转的镜片。
    struct BusPainter
    {
        int width, height;
        // 遮罩仅在切换镜片时光栅化一次。每个窗户/灯带直接查跨度，不再重新扫描镜片所有边。
        static constexpr int ROWS=HAL::DisplayConfig::LOGICAL_HEIGHT;
        int16_t maskLeft[ROWS],maskRight[ROWS];
        int maskTop=0,maskBottom=-1;
        bool hasMask=false;
        bool hasBand=false;
        float bandStart=0,bandEnd=0,bandSlope=0;
        float opacity=1;
        float brightness=1;
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

        void SetMask(const Polygon *p)
        {
            hasMask=p!=nullptr;
            if (!p) return;
            const RasterPolygon boundary(*p,width,height);
            maskTop=max(0,boundary.top); maskBottom=min(min(height,ROWS)-1,boundary.bottom);
            for (int row=maskTop;row<=maskBottom;++row)
            {
                float left,right;
                if (p->count>=3&&boundary.Span(row,left,right))
                { maskLeft[row]=(int16_t)max(0,(int)ceilf(left)); maskRight[row]=(int16_t)min(width-1,(int)floorf(right)); }
                else { maskLeft[row]=1; maskRight[row]=0; }
            }
        }
        uint16_t Shade(uint16_t color) const
        {
            if (brightness>=1) return color;
            const int amount=(int)(Clamp(brightness)*255);
            return (uint16_t)(((((color>>11)&31)*amount/255)<<11)|
                              ((((color>>5)&63)*amount/255)<<5)|((color&31)*amount/255));
        }
        void SetBand(float start,float end,float slope)
        {
            hasBand=true;
            bandStart=start*width/428.0f; bandEnd=end*width/428.0f;
            bandSlope=slope*(width/428.0f)/(height/142.0f);
        }
        void ClearBand() { hasBand=false; }
        void Span(int x0,int x1,int row,uint16_t color,uint8_t alpha) const
        {
            if (hasMask)
            {
                if (row<maskTop||row>maskBottom) return;
                x0=max(x0,(int)maskLeft[row]); x1=min(x1,(int)maskRight[row]);
            }
            if (hasBand)
            {
                x0=max(x0,(int)ceilf(bandStart-row*bandSlope));
                x1=min(x1,(int)floorf(bandEnd-row*bandSlope));
            }
            x0=max(0,x0); x1=min(width-1,x1);
            if (x1<x0) return;
            if (alpha==255) HAL_Fill_Rect(x0,row,x1-x0+1,1,color);
            else if (alpha) HAL_Blend_Rect(x0,row,x1-x0+1,1,color,alpha);
        }

        void Paint(const Polygon &p, uint16_t color) const
        {
            if (opacity<=0 || p.count<3) return;
            // 车窗图元/镜片均为凸多边形：逐行求图元与遮罩的公共跨度即可，不必构造多次
            // 64 顶点临时交集。这既保留 RGB565 整数光栅，也缩短嵌套调用的主循环栈链。
            const RasterPolygon shape(p,width,height);
            const uint8_t alpha=(uint8_t)lroundf(Clamp(opacity)*255);
            color=Shade(color);
            const int top=max(0,hasMask?max(shape.top,maskTop):shape.top);
            const int bottom=min(height-1,hasMask?min(shape.bottom,maskBottom):shape.bottom);
            for (int row=top;row<=bottom;++row)
            {
                float left,right;
                if (shape.Span(row,left,right)) Span((int)ceilf(left),(int)floorf(right),row,color,alpha);
            }
        }
        void Poly(std::initializer_list<Point> points, uint16_t color) const
        {
            Polygon p;
            for (Point v:points) p.points[p.count++]=v;
            Paint(p,color);
        }
        void Box(float x,float y,float w,float h,uint16_t color) const
        {
            if (w<=0||h<=0||opacity<=0) return;
            // 矩形不再走通用四边形逐行求交；无掩模的街景窗户直接提交一次矩形。
            const int x0=max(0,(int)lroundf(x*width/428)),x1=min(width-1,(int)lroundf((x+w)*width/428));
            const int top=max(0,(int)lroundf(y*height/142)),bottom=min(height-1,(int)lroundf((y+h)*height/142)-1);
            if (x1<x0||bottom<top) return;
            color=Shade(color);
            const uint8_t alpha=(uint8_t)lroundf(Clamp(opacity)*255);
            if (!hasMask&&!hasBand)
            {
                if (alpha==255) HAL_Fill_Rect(x0,top,x1-x0+1,bottom-top+1,color);
                else if (alpha) HAL_Blend_Rect(x0,top,x1-x0+1,bottom-top+1,color,alpha);
                return;
            }
            for (int row=hasMask?max(top,maskTop):top;row<=(hasMask?min(bottom,maskBottom):bottom);++row)
                Span(x0,x1,row,color,alpha);
        }
        void Line(float x,float y,float xx,float yy,uint16_t color,float thickness=1) const
        {
            const float length=hypotf(xx-x,yy-y);
            if (length<.001f) { Box(x,y,thickness,thickness,color); return; }
            const float nx=-(yy-y)/length*thickness*.5f,ny=(xx-x)/length*thickness*.5f;
            Poly({{x+nx,y+ny},{xx+nx,yy+ny},{xx-nx,yy-ny},{x-nx,y-ny}},color);
        }

        // 街景与碎镜分段调用，禁止把大几何临时数组一起内联进最外层帧函数。
        __attribute__((noinline)) void Scene(double seconds) const
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
            // 城市属于镜面底层，颜色在调色板级混合；灯带/扶手最后覆盖。
            // 避免十块镜片内每个城市窗户都执行逐像素透明读回。
            if (t>.65f)
            {
                const float strength=Smooth((t-.65f)/.45f)*.7f;
                const auto blend=[&](uint16_t foreground) {
                    const int q=(int)(strength*255),inverse=255-q;
                    return (uint16_t)((((((foreground>>11)&31)*q+((base>>11)&31)*inverse)/255)<<11)|
                                      (((((foreground>>5)&63)*q+((base>>5)&63)*inverse)/255)<<5)|
                                      (((foreground&31)*q+(base&31)*inverse)/255));
                };
                const uint16_t cityBase=blend(colored?base:RGB(24,34,39));
                const uint16_t cityWindow=blend(colored?palette.tint:RGB(96,110,113));
                const float horizon=72+sinf(index*2.3f)*14;
                for (int j=0;j<9;++j)
                {
                    const int x=j*57-20+(index*19)%38,h=13+(j*17+index*11)%35;
                    Box(x,horizon-h,43,h,cityBase);
                    for (int k=0;k<3;++k)
                        Box(x+6+k*10,horizon-h+7,3,5,cityWindow);
                }
            }
            const float drift=sinf((float)fmod(world*.7,6.283185307)+index*.4f)*3;
            Poly({{70+drift,14},{192+drift,17},{307+drift,103},{268+drift,107}},tint);
            Poly({{122+drift,19},{152+drift,20},{273+drift,107},{258+drift,107}},
                 colored?palette.bright:RGB(186,194,194));
            Poly({{292,106},{305,65},{320,58},{343,62},{369,112}},colored?base:RGB(24,30,34));
            Line(315,65,340,68,colored?palette.tint:RGB(84,92,94),2);
            Line(63,99,387,91,colored?palette.tint:RGB(77,88,90),2);

        }
    };

    Point GlassPoint(Point p, float zoom)
    {
        const float u=p.x/428,v=p.y/142;
        return {Mix(Mix(Mix(34,410,u),Mix(38,410,u),v),p.x,zoom),
                Mix(Mix(Mix(22,11,u),Mix(113,104,u),v),p.y,zoom)};
    }
    // 将单片投影临时多边形与外层街景/车窗分栈，避免两阶段最坏栈占用相加。
    __attribute__((noinline)) void DrawBusShard(const Shard &shard,int index,BusPainter &painter,
                                               float t,double world,float crack,float zoom,float flip,float beam,
                                               const Polygon &glass)
    {
        // Start 缓存的分区属于实际画布，先还原设计坐标，再映射到透视车窗/放大后的镜面。
        Polygon cell=shard.cell;
        for (int j=0;j<cell.count;++j)
        { cell.points[j].x*=428.0f/painter.width; cell.points[j].y*=142.0f/painter.height; }
        if (t<.68f)
        {
            if (crack<=0) return;
            painter.SetMask(&glass);
            for (int j=0;j<cell.count;++j)
            {
                const Point a=GlassPoint(cell.points[j],0),b=GlassPoint(cell.points[(j+1)%cell.count],0);
                painter.Line(a.x,a.y,Mix(a.x,b.x,crack),Mix(a.y,b.y,crack),RGB(164,179,180));
            }
            return;
        }
        Polygon p=Within(cell,cell,2.3f*crack);
        const Point origin={shard.origin.x*428/painter.width,shard.origin.y*142/painter.height};
        const float ax=cosf(shard.axis),ay=sinf(shard.axis),cs=cosf(flip*kPi*.5f);
        for (int j=0;j<p.count;++j)
        {
            const float dx=p.points[j].x-origin.x,dy=p.points[j].y-origin.y;
            const float u=dx*ax+dy*ay,v=-dx*ay+dy*ax;
            p.points[j]={origin.x+u*cs*ax-v*ay,origin.y+u*cs*ay+v*ax};
        }
        p=Within(p,cell,0);
        for (int j=0;j<p.count;++j) p.points[j]=GlassPoint(p.points[j],zoom);
        if (Area(p)<.15f) return;
        painter.SetMask(&p);
        painter.SetBand(beam,1000,.3f);
        painter.Reflection(t,world,shard.appearance,false,index);
        painter.SetBand(-1000,beam,.3f);
        painter.Reflection(t,world,shard.appearance,true,index);
        if (t>.73f&&t<1.73f)
        {
            painter.SetBand(beam-4,beam+7,.3f);
            painter.Box(0,0,428,142,RGB(225,233,233));
            painter.SetBand(beam-1,beam+1,.3f);
            painter.Box(0,0,428,142,TFT_WHITE);
        }
        painter.SetMask(nullptr);
        painter.ClearBand();
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
    }

}

void UIGacha::ExtractionAnimation::DrawBusFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const
{
    // 演出时钟与行驶时钟分离：待抽多久都持续行驶，SetPulls/开始/掉帧均不重置世界坐标。
    // double 只用于长时间累计位移，避免待机数小时后 float 毫秒时间丢失短帧精度。
    const double world=motion_ms*.001;
    const float t=idle?-1:elapsed_ms*(.001f*4/3);
    const float crack=Smooth((t-.22f)/.40f),zoom=Smooth((t-1.95f)/.35f);
    const float flip=Smooth((t-2.3f)/.5f),beam=Mix(-25,500,Clamp((t-.73f)/1.0f));
    BusPainter painter{width_,height_};
    // 四角直接构造，避免 Rect→GlassPolygon 的按值返回在 ESP32 调用栈上暂存两份大多边形。
    Polygon glass;
    glass.count=4;
    glass.points[0]={34,22}; glass.points[1]={410,11};
    glass.points[2]={410,104}; glass.points[3]={38,113};
    if (idle || elapsed_ms<BUS_FLIP_START_MS)
    {
        if (idle||t<.51f) painter.Scene(world);
        if (!idle)
        {
            painter.SetMask(&glass);
            // 整窗倒影本身已完成外景到镜面的渐变，不再先叠一层全窗透明黑再叠倒影。
            // 同一片玻璃每帧少一次整区域 RGB565 读改写。
            if (t>=.51f)
            {
                painter.opacity=1;
                painter.Box(0,0,428,142,RGB(3,8,11));
            }
            painter.SetMask(nullptr);
        }
        painter.opacity=1;
    }
    if (idle) { painter.Interior(); return; }
    if (t<.68f)
    {
        painter.SetMask(&glass); painter.opacity=Smooth((t-.06f)/.45f);
        painter.Reflection(t,world,{},false,0);
        painter.opacity=1;
    }
    for (int index=0;index<PULL_COUNT;++index)
        DrawBusShard(shards_[index],index,painter,t,world,crack,zoom,flip,beam,glass);
    // 镜面放大前车内结构始终压在玻璃上，避免倒影与裂纹穿过座椅/锁扣。
    if (zoom<1)
    { painter.SetMask(nullptr); painter.opacity=1; painter.brightness=1-zoom; painter.Interior(); }
}
