/*
【模块职责】抽卡演出的统一入口、布局生成与公共凸多边形工具。
【文件分工】gacha_mirror.cpp 维护散落碎镜；gacha_bus.cpp 维护行驶车窗；
本文件共享几何/配色，并绘制翻面后露出的打印清单；不处理 App 输入、抽卡概率、配置或文件资源。
【约束】UI 主循环调用，固定容量布局，一次 Start 缓存后逐帧复用。
*/
#include "gacha_internal.h"
#include "hal/hal.h"
#include "hal/hal_display_config.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

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

namespace
{
    namespace ReceiptLayout = UITheme::GachaReceipt;
    constexpr uint16_t ReceiptInk=0xbdd7, ReceiptMuted=0x6bcf, ReceiptEdge=0x1945;
    constexpr uint16_t ReceiptPaper=0x0842, ReceiptSelection=0x10c4;

    uint16_t ReceiptDim(uint16_t color, float light)
    {
        light=Clamp(light);
        return (uint16_t)(((int)(((color>>11)&31)*light)<<11) |
                          ((int)(((color>>5)&63)*light)<<5) | (int)((color&31)*light));
    }

    uint16_t ReceiptRarity(UIGacha::PullAppearance appearance)
    {
        return appearance.walp ? 0x7f51 : appearance.star==3 ? 0xfdec :
               appearance.star==2 ? 0xf38c : appearance.star==1 ? 0xbdf8 : ReceiptMuted;
    }

    int ReceiptTop(int selected)
    {
        const int visible=ReceiptLayout::VisibleRows(HAL_Get_Screen_Height());
        return constrain(selected-1,0,UIGacha::PULL_COUNT-visible);
    }

    float ReceiptTargetPosition(int row, int selected)
    {
        return (row-ReceiptTop(selected))*ReceiptLayout::RowHeight() +
               (row>selected ? ReceiptLayout::ExpansionHeight() : 0);
    }

    // 几何先裁切，文字只交完整 UTF-8 字形给 UIText；纸面上下沿统一遮罩。
    void ReceiptBox(int x,int y,int w,int h,uint16_t color,int width,int height)
    {
        const int right=min(width,x+w), bottom=min(height,y+h);
        x=max(0,x); y=max(0,y);
        if (right>x && bottom>y) HAL_Fill_Rect(x,y,right-x,bottom-y,color);
    }

    void ReceiptLabel(int x,int y,const String &text,uint16_t color,int width,int height)
    {
        // 宿主和设备共用屏边界规则；垂直半字不交给字体层，避免上/下滑时写到画布外。
        if (y<0 || y+HAL_Get_Font_Line_Height(HAL_FONT_BODY)>height) return;
        const String fitted=UIText::FitLineWithEllipsis(text,width);
        HAL_Screen_ShowLine_Font(x,y,fitted.c_str(),HAL_FONT_BODY,color);
    }

    void ReceiptMarks(int x,int y,UIGacha::PullAppearance appearance,uint16_t color,int width,int height)
    {
        for (int star=0;star<3;++star)
            ReceiptBox(x+star*5,y,3,3,star<appearance.star ? color : ReceiptEdge,width,height);
    }
}

void UIGacha::ResultReceipt::Reset(int selected,uint32_t now_ms)
{
    selected_=constrain(selected,0,PULL_COUNT-1);
    selection_time_=detail_time_=now_ms; moving_=false; printing_=false; detail_overflow_=0;
    for (int row=0;row<PULL_COUNT;++row)
    {
        from_position_[row]=ReceiptTargetPosition(row,selected_);
        from_expansion_[row]=row==selected_ ? 1.f : 0.f;
    }
}

float UIGacha::ResultReceipt::RowPosition(int row,uint32_t now_ms) const
{
    const float q=moving_ ? Smooth((now_ms-selection_time_)/(float)SELECT_MS) : 1.f;
    return Mix(from_position_[row],ReceiptTargetPosition(row,selected_),q);
}

float UIGacha::ResultReceipt::Expansion(int row,uint32_t now_ms) const
{
    const float q=moving_ ? Smooth((now_ms-selection_time_)/(float)SELECT_MS) : 1.f;
    return Mix(from_expansion_[row],row==selected_ ? 1.f : 0.f,q);
}

void UIGacha::ResultReceipt::Select(int selected,uint32_t now_ms)
{
    selected=constrain(selected,0,PULL_COUNT-1);
    if (selected==selected_) return;
    // 高频连续旋转从当前实际位置接续，不能把上一格未完成的纸面运动重置回起点。
    for (int row=0;row<PULL_COUNT;++row)
    {
        from_position_[row]=RowPosition(row,now_ms);
        from_expansion_[row]=Expansion(row,now_ms);
    }
    selected_=selected; selection_time_=detail_time_=now_ms; moving_=true; detail_overflow_=0;
}

bool UIGacha::ResultReceipt::IsMoving(uint32_t now_ms) const
{
    return moving_ && now_ms-selection_time_<SELECT_MS;
}

void UIGacha::ResultReceipt::Draw(const ReceiptRecord (&records)[PULL_COUNT],const char *title,
                                 int width,int height,uint32_t entry_ms,uint32_t now_ms)
{
    if (width<280 || height<100) return;
    // 入场和行切换尚未结束时不消耗长名阅读停留，完整清单停稳后才计 900ms。
    if (entry_ms<ENTRY_MS || IsMoving(now_ms)) detail_time_=now_ms;
    const float q=Smooth(entry_ms/(float)ENTRY_MS);
    const int paperOffset=(int)lroundf(-12*(1-q));
    const int rowHeight=ReceiptLayout::RowHeight(), detailStep=ReceiptLayout::DetailStep();
    const int headerHeight=ReceiptLayout::HeaderHeight(), listBottom=height-6;
    const auto box=[&](int x,int y,int w,int h,uint16_t color) { ReceiptBox(x,y,w,h,color,width,height); };
    box(5,0,width-10,height,ReceiptDim(ReceiptPaper,q));
    box(5,0,1,height,ReceiptDim(ReceiptEdge,q)); box(width-6,0,1,height,ReceiptDim(ReceiptEdge,q));
    for (int y=headerHeight+6;y<height-5;y+=12)
    { box(8,y,2,3,ReceiptDim(0x4228,q)); box(width-11,y,2,3,ReceiptDim(0x4228,q)); }

    detail_overflow_=0;
    for (int row=0;row<PULL_COUNT;++row)
    {
        const int y=ReceiptLayout::ListTop()+(int)lroundf(RowPosition(row,now_ms))+paperOffset;
        const float expanded=Expansion(row,now_ms);
        const int h=rowHeight+(int)lroundf(expanded*ReceiptLayout::ExpansionHeight());
        if (y+h<headerHeight || y>listBottom) continue;
        const auto &record=records[row];
        const uint16_t color=ReceiptDim(ReceiptRarity(record.appearance),q);
        if (expanded>0)
        {
            box(14,y-1,width-28,h,ReceiptDim(ReceiptSelection,q*expanded));
            box(14,y+1,2,h-5,ReceiptDim(color,expanded));
        }
        char number[4]; snprintf(number,sizeof(number),"%02d",row+1);
        ReceiptLabel(22,y,number,ReceiptDim(expanded>.5f ? ReceiptInk : ReceiptMuted,q),24,height);
        ReceiptMarks(51,y+8,record.appearance,color,width,height);
        // 瓦夜增加 W 标识，仍保留真实的一至三条星级刻线，避免只凭颜色区分。
        if (record.appearance.walp) ReceiptLabel(67,y,"W",color,10,height);
        String text=record.sinner;
        if (expanded<=.5f) { text+="  "; text+=record.identity; }
        ReceiptLabel(77,y,text,ReceiptDim(expanded>.5f ? ReceiptInk : 0x9cf3,q),width-101,height);
        if (expanded>.5f && y+detailStep>=0 && y+detailStep+HAL_Get_Font_Line_Height(HAL_FONT_BODY)<=height)
        {
            const int textWidth=width-101;
            const int overflow=max(0,UIText::Measure(record.identity)-textWidth);
            if (row==selected_) detail_overflow_=overflow;
            int offset=0;
            if (row==selected_ && overflow>0 && !IsMoving(now_ms))
            {
                // 停留 900ms 后以 24px/s 往返，端点各停 900ms；只滚动选中人格名。
                const uint32_t travel=max(1U,(uint32_t)overflow*1000/24);
                const uint32_t clock=(now_ms-detail_time_)%(2*travel+1800);
                if (clock>900 && clock<=900+travel) offset=(clock-900)*overflow/travel;
                else if (clock>900+travel && clock<=1800+travel) offset=overflow;
                else if (clock>1800+travel) offset=overflow-(clock-1800-travel)*overflow/travel;
            }
            UIText::DrawClippedColor(77-offset,y+detailStep,record.identity,ReceiptDim(color,expanded),77,width-24);
        }
        box(22,y+h-2,width-43,1,ReceiptDim(ReceiptEdge,q));
    }

    // 页眉在行绘制之后恢复，纸面上下沿同时裁掉运动中的残留文字。
    box(14,0,width-28,headerHeight,TFT_BLACK);
    box(14,listBottom,width-28,height-listBottom,ReceiptDim(ReceiptPaper,q));
    const int countsX=width-206;
    ReceiptLabel(17,0,title,ReceiptDim(ReceiptInk,q),max(0,countsX-25),height);
    int counts[4]={};
    for (const auto &record:records)
        if (record.appearance.star>=1 && record.appearance.star<=3) ++counts[record.appearance.star];
    for (int star=3;star>=1;--star)
    {
        const int x=countsX+(3-star)*64;
        PullAppearance appearance; appearance.star=(uint8_t)star;
        ReceiptMarks(x,9,appearance,ReceiptDim(ReceiptRarity(appearance),q),width,height);
        char count[4]; snprintf(count,sizeof(count),"%d",counts[star]);
        ReceiptLabel(x+21,0,count,ReceiptDim(ReceiptInk,q),35,height);
    }
    box(15,headerHeight-4,width-29,1,ReceiptDim(0x3269,q));
    if (entry_ms<ENTRY_MS)
    {
        const int head=headerHeight+(int)(q*(listBottom-headerHeight));
        box(14,head,width-28,listBottom-head,ReceiptDim(ReceiptPaper,q));
        box(15,head,width-30,1,ReceiptDim(0x6e9b,1-q));
        box(15,head+1,width-30,1,ReceiptDim(0x6e9b,(1-q)*.25f));
    }
    box(15,height-4,width-29,1,ReceiptDim(ReceiptEdge,q));
    for (int i=0;i<PULL_COUNT;++i) box(15+i*(width-28)/PULL_COUNT,height-2,(width-28)/PULL_COUNT/2,1,ReceiptDim(ReceiptEdge,q));
    printing_=entry_ms<ENTRY_MS;
    if (!IsMoving(now_ms)) moving_=false;
}
