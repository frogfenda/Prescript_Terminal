/*
【模块职责】双红绳的松弛跟随、编绳曲线和绘制；不解释菜单输入，不绘制连接标记。
【线程】全部由主循环调用；使用固定数量控制点，不申请内存、不读取资源或 IMU。
【坐标】material 为未取模的菜单槽位，控制点必须按槽位递增；dt 单位秒。
*/
#pragma once
#include "ui/ui_menu_motion.h"

struct UIMenuRopeAnchor { float x, y, material; };
struct UIMenuRopeSample { float x, y, depth, material; };

class UIMenuRopes
{
    float phase_ = 0.0f;
    float lag_ = 0.0f;
    float lag_velocity_ = 0.0f;
    float tension_ = 0.0f;

    static float Curve(float p0, float p1, float p2, float p3, float t)
    {
        return p1 + 0.5f * t * (p2 - p0 + t * (2*p0 - 5*p1 + 4*p2 - p3 +
                                              t * (-p0 + 3*p1 - 3*p2 + p3)));
    }
    static float Tangent(float p0, float p1, float p2, float p3, float t)
    {
        return 0.5f * (p2 - p0 + 2*t * (2*p0 - 5*p1 + 4*p2 - p3) +
                       3*t*t * (-p0 + 3*p1 - 3*p2 + p3));
    }
public:
    /** 页面进入时清空全部视觉状态；直接跳转菜单位置时只清动量，保留闲置摆动相位。 */
    void reset() { phase_ = lag_ = lag_velocity_ = tension_ = 0.0f; }
    void clearMomentum() { lag_ = lag_velocity_ = tension_ = 0.0f; }
    /** 相对于文字的滞后槽位，供布局计算隐形控制点；只影响绳子，不反向修改菜单状态。 */
    float followOffset() const { return lag_; }
    /** 每次菜单动画更新调用；速度单位槽/秒，dt 单位秒，不读取时钟或触发推屏。 */
    void update(float scroll_velocity, float dt)
    {
        if (dt <= 0.0f) return;
        dt = fminf(dt, 0.1f);
        phase_ = fmodf(phase_ + dt * 0.9f, 6.2831853f);
        // 正向滚动时文字上移，绳体沿路径稍往下滞后；速度反向和停下均保留阻尼连续性。
        const float target_lag = fmaxf(-0.20f, fminf(0.20f, scroll_velocity * 0.009f));
        UI_MenuAdvanceDamped(target_lag, dt, 16.0f, lag_, lag_velocity_);
        UI_MenuApproach(fabsf(scroll_velocity) / (fabsf(scroll_velocity) + 9.0f), dt, 10.0f, tension_);
    }

    /** 纯几何采样；parameter 是控制点数组的连续下标，strand 为 0/1。少于两点不绘制。 */
    UIMenuRopeSample sample(const UIMenuRopeAnchor *anchors, int count, float parameter, int strand) const
    {
        if (!anchors || count < 2) return {0, 0, 0, 0};
        const float u = fmaxf(0.0f, fminf((float)(count - 1), parameter));
        int i = (int)floorf(u);
        if (i >= count - 1) i = count - 2;
        const int prev = i > 0 ? i - 1 : 0;
        const int next = i + 2 < count ? i + 2 : count - 1;
        const float t = u - i;
        const auto &a = anchors[prev], &b = anchors[i], &c = anchors[i + 1], &d = anchors[next];
        const float x = Curve(a.x,b.x,c.x,d.x,t), y = Curve(a.y,b.y,c.y,d.y,t);
        const float dx = Tangent(a.x,b.x,c.x,d.x,t), dy = Tangent(a.y,b.y,c.y,d.y,t);
        const float length = fmaxf(0.01f, sqrtf(dx*dx + dy*dy));
        const float material = b.material + (c.material - b.material) * t;
        // 取模只用于周期波形，不用于菜单位置。循环坐标归一化后编绳相位保持不变。
        const float theta = fmodf(material, 1.0f) * 6.2831853f + phase_;
        const float sway = sinf(phase_ - fmodf(material, 1.0f) * 6.2831853f) * 1.3f;
        const float sign = strand ? -1.0f : 1.0f;
        const float weave = sign * (4.5f - tension_ * 1.5f) * cosf(theta) + sway;
        return {x + dy/length * weave, y - dx/length * weave, sign * sinf(theta), material};
    }

    /**
     * 背绳先画、前绳后画；clip_left/right 保护 HUD 和屏幕边界，右边界不包含在内。
     * 在菜单文字之前调用，不主动推屏；不裁掉文字矩形，由后续透明字体仅覆盖字形笔画。
     */
    void draw(const UIMenuRopeAnchor *anchors, int count, float visual_position,
              int clip_left, int clip_right) const;
};
