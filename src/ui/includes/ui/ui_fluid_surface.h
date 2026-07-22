/*
【模块职责】提供适合 428×142 长条屏的多层海面、雨滴和闪电反光模拟与暗色写实风格 RGB565 绘制。
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

/**
 * 海面天气输入。天气不是传感器数据，因此与 UIFluidInput 分开封装，但由同一个帧输入传递，
 * 避免 update() 增加一串位置参数。lightning_flash 由 App 的非阻塞雷暴状态机控制在 0~1。
 */
struct UIFluidWeatherInput
{
    bool raining = false;
    float rain_intensity = 0.0f; // 雨势，0~1；UI 内部会再次钳制和缓变。
    float lightning_flash = 0.0f; // 闪电亮度，0~1；不直接代表雷声。
    uint32_t lightning_seed = 0; // 每次雷击变化一次，用于决定闪电和水面反光位置。
};

/** 单帧统一输入：运动由 AppSea 解算，天气由 AppSea 的事件状态机维护。 */
struct UIFluidFrameInput
{
    UIFluidInput motion;
    UIFluidWeatherInput weather;
};

/** 固定容量雨滴环形池；只保存尚未衰减完的落点，不申请动态内存。 */
struct RainImpact
{
    float x = 0.0f;
    float phase = 0.0f;
    float strength = 0.0f;
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
     * 推进一步多层海面和天气状态。
     * dt_seconds 为本次数值步长，过大步长会在内部钳制；motion.valid=false 时海面逐渐回中，
     * 但仍保留风浪、雨滴局部涟漪和约 16 秒一周期的缓慢潮汐，不会退化为静止色块。
     * 当前天气输入只驱动视觉；声音由 AppSeaAudioBinding 作为可选回调预留，不在 UI 层播放。
     */
    void update(float dt_seconds, const UIFluidFrameInput &input);

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
    float layer_roll_state_[LAYER_COUNT] = {};
    float layer_roll_velocity_[LAYER_COUNT] = {};
    float phase_seconds_ = 0.0f;
    float tide_phase_seconds_ = 0.0f;
    float motion_energy_ = 0.0f;
    float edge_impact_energy_[2] = {}; // 0=左侧、1=右侧；驱动短时泡沫和飞沫强度。
    float edge_impact_y_[2] = {};      // 最近一次撞击高度，单位：屏幕像素 y。
    RainImpact rain_impacts_[12] = {};
    uint32_t rain_random_state_ = 0x6D2B79F5UL;
    uint8_t rain_impact_cursor_ = 0;
    float rain_spawn_accumulator_ = 0.0f;
    float filtered_rain_intensity_ = 0.0f;
    float lightning_flash_ = 0.0f;
    uint32_t lightning_seed_ = 0;
    int lightning_x_ = 0;

    float baseSurfaceY(int layer) const;
    float normalizedNodeX(int index) const;
    int surfacePixelAtNode(int layer, int index) const;
    int surfacePixelAtX(int layer, int x) const;
    void applyEdgeCollisions(float dt_seconds);
    uint32_t nextRainRandom();
    void updateRainImpacts(float dt_seconds, float rain_intensity);
    float rainRippleForce(int layer, int node_index) const;
    void drawStormSky() const;
    void drawWaveLayer(int layer) const;
    // 绘制由倾斜、横向冲量和上下摇动驱动的连续波脊；泡沫只负责波峰高光，不能替代波形本身。
    void drawResponsiveWaves() const;
    void drawWaterDetails() const;
    void drawRain() const;
    void drawEdgeCollisions() const;
};
