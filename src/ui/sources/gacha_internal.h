/*
【模块职责】抽卡两个渲染实现的私有几何/调色板契约；不作为其它 App 的绘图框架。
【约束】凸多边形、固定顶点容量；返回的多边形为值副本，不持有调用方临时内存。
*/
#pragma once
#include "ui/ui_gacha.h"
#include <math.h>
namespace UIGacha { namespace Detail
{
    constexpr float kPi=3.14159265f;
    float Clamp(float t);
    float Smooth(float t);
    float Mix(float a,float b,float t);
    // 编译期量化固定调色板，逐帧不再为每个窗户/线段调用 lroundf。
    constexpr uint16_t RGB(int r,int g,int b)
    { return (uint16_t)((((r*31+127)/255)<<11)|(((g*63+127)/255)<<5)|((b*31+127)/255)); }
    float Next(uint32_t &seed);
    Polygon Rect(float x0,float y0,float x1,float y1);
    Polygon Clip(const Polygon &p,float a,float b,float d);
    float Area(const Polygon &p);
    Point Center(const Polygon &p);
    Polygon Within(Polygon p,const Polygon &boundary,float inset);
    void Edge(Point a,Point b,uint16_t color);
    void PrepareMirrorShard(Shard &s,uint32_t &seed,float sx,float sy);
    struct Palette { uint16_t base,dark,mid,tint,bright; };
    Palette Colors(PullAppearance appearance,bool colored,float light);
}}
