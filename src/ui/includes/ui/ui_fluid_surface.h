/*
【模块职责】提供适合 428×142 长条屏的多层海面模拟与暗色写实风格 RGB565 绘制。
【能力边界】本模块只消费 App 已解算好的连续运动量，不读取 IMU、不管理 App 生命周期，
也不绘制字幕等业务前景；调用方必须在 draw() 之后绘制最高层内容。
【资源约束】四层浪带共用固定节点和单份下一帧暂存区，不申请第二块全屏缓冲区，最终复用 HAL Sprite。
*/
#pragma once

#include <Arduino.h>

#include "sys/sys_constants.h"

/**
 * 海面连续输入。
 *
 * 所有字段均由上层基于同一份 SysMotion 样本计算：姿态决定长期水准面，角速度和角加速度
 * 决定转动惯性，去重力后的线性加速度决定冲击、回摆与碎浪。valid=false 时其余字段会被忽略。
 */
struct UIFluidInput
{
    float roll_deg = 0.0f;             // 左右倾角，单位：度。
    float roll_rate_dps = 0.0f;        // 绕机身 X 轴角速度，单位：度/秒。
    float roll_accel_dps2 = 0.0f;      // 绕机身 X 轴角加速度，单位：度/秒²。
    float lateral_accel_g = 0.0f;      // 机身 Y 轴去重力后的线性加速度，单位：g。
    float vertical_accel_g = 0.0f;     // 机身 Z 轴去重力后的线性加速度，单位：g。
    bool valid = false;
};

class UIFluidSurface
{
public:
    /**
     * 按当前逻辑画布尺寸重置全部浪带、潮汐相位和运动能量。
     * width/height 单位为像素；非法尺寸会让后续 update()/draw() 安全地保持空操作。
     */
    void reset(int width, int height);

    /**
     * 推进一步多层海面状态。
     * dt_seconds 为本次数值步长，过大步长会在内部钳制；input.valid=false 时海面逐渐回中，
     * 但仍保留风浪和约 16 秒一周期的缓慢潮汐，不会退化为静止色块。
     */
    void update(float dt_seconds, const UIFluidInput &input);

    /**
     * 按“风暴天空→远海→两层中景浪→前景潮水→碎浪/飞沫”的顺序绘制到 HAL Sprite。
     * 本函数不清空 Sprite、不推送屏幕，调用方应在同一帧最后统一 HAL_Screen_Update()。
     */
    void draw() const;

private:
    static constexpr int NODE_SPACING_PX = 4;
    static constexpr int LAYER_COUNT = 4;
    static constexpr int MAX_NODES =
        (PrescriptConst::UI_SCREEN_WIDTH + NODE_SPACING_PX - 1) / NODE_SPACING_PX + 1;

    int width_ = 0;
    int height_ = 0;
    int node_count_ = 0;
    float surface_y_[LAYER_COUNT][MAX_NODES] = {};
    float velocity_y_[LAYER_COUNT][MAX_NODES] = {};
    float next_surface_y_[MAX_NODES] = {};
    float filtered_roll_deg_ = 0.0f;
    float filtered_roll_rate_dps_ = 0.0f;
    float filtered_roll_accel_dps2_ = 0.0f;
    float filtered_lateral_accel_g_ = 0.0f;
    float filtered_vertical_accel_g_ = 0.0f;
    float phase_seconds_ = 0.0f;
    float tide_phase_seconds_ = 0.0f;
    float motion_energy_ = 0.0f;
    float edge_impact_energy_[2] = {}; // 0=左侧、1=右侧；驱动短时泡沫和飞沫强度。
    float edge_impact_y_[2] = {};      // 最近一次撞击高度，单位：屏幕像素 y。

    float baseSurfaceY(int layer) const;
    float normalizedNodeX(int index) const;
    int surfacePixelAtNode(int layer, int index) const;
    int surfacePixelAtX(int layer, int x) const;
    void applyEdgeCollisions(float dt_seconds);
    void drawStormSky() const;
    void drawWaveLayer(int layer) const;
    void drawWaterDetails() const;
    void drawEdgeCollisions() const;
};
