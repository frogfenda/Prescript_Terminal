/*
【模块职责】菜单视觉坐标的阻尼运动和布局渐变；不处理输入、索引取模或页面跳转。
【调用约束】主循环以秒为单位传入两帧间隔；位置使用连续槽位，速度使用槽位/秒。
临界阻尼使用解析解，连续输入只改变目标，不重置速度，掉帧时也不会因欧拉积分发散。
*/
#pragma once
#include <math.h>

inline bool UI_MenuAdvanceDamped(float target, float dt, float response,
                                float &position, float &velocity)
{
    if (dt <= 0.0f)
        return false;
    if (fabsf(target - position) < 0.002f && fabsf(velocity) < 0.02f)
    {
        const bool changed = position != target || velocity != 0.0f;
        position = target;
        velocity = 0.0f;
        return changed;
    }
    const float displacement = position - target;
    const float b = velocity + response * displacement;
    const float decay = expf(-response * dt);
    position = target + (displacement + b * dt) * decay;
    velocity = (velocity - response * b * dt) * decay;
    return true;
}

/** 标量布局的时间归一化渐变，接近终点时精确归位，避免静止页面永久重绘。 */
inline bool UI_MenuApproach(float target, float dt, float response, float &value)
{
    if (value == target || dt <= 0.0f)
        return false;
    value += (target - value) * (1.0f - expf(-response * dt));
    if (fabsf(target - value) < 0.002f)
        value = target;
    return true;
}
