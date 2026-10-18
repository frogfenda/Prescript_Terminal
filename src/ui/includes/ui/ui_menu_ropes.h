/*
【模块职责】菜单/编辑导轨共用双红绳的松弛跟随、曲线和绘制；不解释输入或绘制连接标记。
【线程】全部由主循环调用；使用固定数量控制点，不申请内存、不读取资源或 IMU。
【坐标】material 为未取模的菜单槽位，控制点必须按槽位递增；dt 单位秒。
*/
#pragma once
#include "ui/ui_menu_motion.h"
#include <stdint.h>

struct UIMenuRopeAnchor { float x, y, material; };
struct UIMenuRopeSample { float x, y, depth, material; };

class UIMenuRopes
{
    float phase_ = 0.0f;
    float lag_ = 0.0f;
    float lag_velocity_ = 0.0f;
    float tension_ = 0.0f;
    float variation_ = 0.0f;
    float noise_time_ = 0.0f;
    uint32_t variation_seed_ = 0x6d2b79f5U;

    // 固定种子二维值噪声：网格间使用五次平滑，位置与速度在边界都连续，不逐帧掷随机数。
    float Noise(float x, float time, uint32_t channel) const
    {
        const int ix = (int)floorf(x), it = (int)floorf(time);
        auto fade = [](float t) { return t*t*t*(t*(t*6.0f-15.0f)+10.0f); };
        auto value = [&](int column, int tick) {
            // 时间格每 256 格环绕，配合 noise_time_ 回绕，长时间待机也不会在浮点精度下降后抖动。
            uint32_t h = variation_seed_ ^ ((uint32_t)column * 0x9e3779b9U) ^
                         (((uint32_t)tick & 255U) * 0x85ebca6bU) ^ (channel * 0xc2b2ae35U);
            h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
            return (h & 65535U) / 32767.5f - 1.0f;
        };
        const float sx = fade(x - ix), st = fade(time - it);
        const float a = value(ix,it), b = value(ix+1,it);
        const float c = value(ix,it+1), d = value(ix+1,it+1);
        return (a + (b-a)*sx)*(1.0f-st) + (c + (d-c)*sx)*st;
    }

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
    void reset() { phase_ = lag_ = lag_velocity_ = tension_ = noise_time_ = 0.0f; }
    /**
     * 页面进入时选择独立噪声种子与 0~1 扰动强度，不消费全局 random() 的业务随机序列。
     * 默认强度为零，原菜单保留既有表现；启用后局部波幅、相位与摆动节奏缓慢变化。
     */
    void setVariation(uint32_t seed, float strength)
    {
        variation_seed_ = seed;
        variation_ = fmaxf(0.0f, fminf(1.0f, strength));
    }
    void clearMomentum() { lag_ = lag_velocity_ = tension_ = 0.0f; }
    /** 相对于文字的滞后槽位，供布局计算隐形控制点；只影响绳子，不反向修改菜单状态。 */
    float followOffset() const { return lag_; }
    /**
     * 将同一编绳模型水平展开；count>=4，首尾各留一个辅助点，内部点横移使波峰间距缓慢伸缩。
     * left/right 为可见端点，y 为基线，cycles 为全宽周期数，offset 为旋钮牵引的周期位移。
     * 纯采样，不推进时钟；两端固定且控制点保持递增，默认未启用 variation 时等间距。
     */
    void horizontalAnchors(UIMenuRopeAnchor *anchors, int count, float left, float right,
                           float y, float cycles, float offset) const
    {
        if (!anchors || count < 4 || right <= left) return;
        const float spacing = (right-left)/(count-3);
        for (int k=0; k<count; ++k)
        {
            // 横移上限为节距的 30%，相邻点最小仍有 40% 节距，避免曲线折返或局部挤成尖角。
            const float drift = k>1 && k<count-2 ?
                variation_*0.30f*spacing*Noise(k*1.37f,noise_time_+k*0.37f,4) : 0.0f;
            anchors[k] = {left+(k-1)*spacing+drift, y,
                          (k-(count-1)*0.5f)*cycles/(count-3)+offset};
        }
    }
    /**
     * 水平导轨专用：在单调递增的控制点上，以屏幕 x 反求连续采样下标；只计算样条 x。
     * 所有点必须处于同一 y，count>=4；不计算编绳噪声/三角函数，供步骤标记定位使用。
     */
    float horizontalParameterAtX(const UIMenuRopeAnchor *anchors, int count, float x) const
    {
        if (!anchors || count<4) return 0.0f;
        float lo=1.0f, hi=(float)(count-2);
        for (int n=0; n<11; ++n)
        {
            const float u=(lo+hi)*0.5f;
            const int i=(int)floorf(u);
            const float xx=Curve(anchors[i-1].x,anchors[i].x,anchors[i+1].x,anchors[i+2].x,u-i);
            if (xx<x) lo=u;
            else hi=u;
        }
        return (lo+hi)*0.5f;
    }
    /** 每次菜单动画更新调用；速度单位槽/秒，dt 单位秒，不读取时钟或触发推屏。 */
    void update(float scroll_velocity, float dt)
    {
        if (dt <= 0.0f) return;
        dt = fminf(dt, 0.1f);
        noise_time_ = fmodf(noise_time_ + dt * 0.22f, 256.0f);
        const float tempo = variation_ > 0.0f ? 1.0f + variation_ * 0.12f * Noise(0.0f,noise_time_,3) : 1.0f;
        phase_ = fmodf(phase_ + dt * 0.9f * tempo, 6.2831853f);
        // 正向滚动时文字上移，绳体沿路径稍往下滞后；速度反向和停下均保留阻尼连续性。
        const float target_lag = fmaxf(-0.20f, fminf(0.20f, scroll_velocity * 0.009f));
        UI_MenuAdvanceDamped(target_lag, dt, 16.0f, lag_, lag_velocity_);
        UI_MenuApproach(fabsf(scroll_velocity) / (fabsf(scroll_velocity) + 9.0f), dt, 10.0f, tension_);
    }

    /** 纯几何采样；parameter 为连续下标，strand 为 0/1；amplitude_scale 缩放法线摆幅，默认菜单原幅度。 */
    UIMenuRopeSample sample(const UIMenuRopeAnchor *anchors, int count, float parameter, int strand,
                            float amplitude_scale = 1.0f) const
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
        float theta = fmodf(material, 1.0f) * 6.2831853f + phase_;
        float sway = sinf(phase_ - fmodf(material, 1.0f) * 6.2831853f) * 1.3f;
        float amplitude = 4.5f - tension_ * 1.5f;
        if (variation_ > 0.0f)
        {
            const float local_amplitude = Noise(u*0.85f,noise_time_,0);
            // 区域相位各自缓慢伸缩，两股再叠加错开的局部节奏；不再只是同一正弦波整体平移。
            theta += variation_ * (1.10f*Noise(u*0.65f,noise_time_,1) +
                0.40f*Noise(u*0.78f,noise_time_+(strand ? 41.37f : 17.83f),5));
            amplitude *= 1.0f + variation_ * (-0.12f + 0.16f*local_amplitude);
            sway = sway*(1.0f-0.5f*variation_) + variation_*0.45f*Noise(u*0.75f,noise_time_,2);
            // 最大偏移仍不超过原来的 5.8px×缩放：随机起伏不能侵入编辑页上下文字。
        }
        const float sign = strand ? -1.0f : 1.0f;
        const float weave = (sign * amplitude * cosf(theta) + sway) *
                            fmaxf(0.0f, amplitude_scale);
        return {x + dy/length * weave, y - dx/length * weave, sign * sinf(theta), material};
    }

    /**
     * 背绳先画、前绳后画；clip_left/right 保护 HUD 和屏幕边界，右边界不包含在内。
     * 在文字之前调用，不主动推屏；不裁掉文字矩形，由后续透明字体仅覆盖字形笔画。
     * amplitude_scale 仅缩放法线方向的波幅；横向导轨可降低波幅而不改变原菜单的默认表现。
     * 主循环串行调用；九控制点以内共享静态几何暂存，禁止跨任务或重入绘制。
     */
    void draw(const UIMenuRopeAnchor *anchors, int count, float visual_position,
              int clip_left, int clip_right, float amplitude_scale = 1.0f) const;
};
