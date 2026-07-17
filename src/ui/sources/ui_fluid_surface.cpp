/*
【模块职责】实现四层低内存一维高度场，并把海面绘制为低饱和、断裂高光、前后尺度不同的暗色海景。
【输入语义】稳定倾角控制水准面；角速度制造涌浪；角加速度制造停止后的反向回摆；去重力后的
横向/垂直加速度制造冲击、潮水抬升和碎浪。各输入只在本模块中统一滤波和钳制。
【数值稳定】物理步长、输入、节点速度和每层活动范围均受限；四层共用 next_surface_y_，按层顺序更新。
*/
#include "ui/ui_fluid_surface.h"

#include <math.h>

#include "hal/hal.h"

namespace
{
    constexpr float PI_F = 3.14159265f;
    constexpr float MAX_ROLL_DEG = 15.0f;
    constexpr float MAX_ROLL_RATE_DPS = 520.0f;
    constexpr float MAX_ROLL_ACCEL_DPS2 = 4200.0f;
    constexpr float MAX_LINEAR_ACCEL_G = 1.35f;
    constexpr float ROLL_FILTER_HZ = 7.0f;
    constexpr float DYNAMIC_FILTER_HZ = 11.0f;
    constexpr float TIDE_PERIOD_SECONDS = 16.0f;
    constexpr float TIDE_RANGE_PX = 7.0f;
    constexpr float MAX_SURFACE_SPEED_PX_S = 230.0f;
    constexpr float EDGE_COLLISION_MIN_RISE_PX = 6.0f;
    constexpr float EDGE_COLLISION_MIN_SPEED_PX_S = 20.0f;
    constexpr float EDGE_COLLISION_DECAY_PER_SECOND = 1.85f;

    // 四层参数从远到近逐渐放大波高、姿态响应与惯性，形成长条屏上可读的透视深度。
    constexpr float BASE_HEIGHT_RATIO[4] = {0.27f, 0.39f, 0.54f, 0.70f};
    constexpr float SLOPE_HALF_RANGE_PX[4] = {10.0f, 18.0f, 30.0f, 42.0f};
    constexpr float AUTO_WAVE_AMPLITUDE_PX[4] = {0.65f, 1.15f, 2.05f, 3.25f};
    constexpr float AUTO_WAVE_SPEED[4] = {0.42f, 0.58f, 0.78f, 1.02f};
    constexpr float AUTO_WAVE_SPATIAL[4] = {0.105f, 0.135f, 0.175f, 0.225f};
    constexpr float RESTORE_STRENGTH[4] = {22.0f, 24.0f, 27.0f, 30.0f};
    constexpr float NEIGHBOR_STRENGTH[4] = {105.0f, 96.0f, 86.0f, 76.0f};
    constexpr float VELOCITY_DAMPING[4] = {5.5f, 5.0f, 4.4f, 3.8f};
    constexpr float RATE_INERTIA[4] = {0.10f, 0.18f, 0.34f, 0.58f};
    constexpr float ANGULAR_ACCEL_INERTIA[4] = {0.002f, 0.005f, 0.011f, 0.021f};
    constexpr float LINEAR_ACCEL_INERTIA[4] = {11.0f, 22.0f, 43.0f, 72.0f};
    constexpr float VERTICAL_ACCEL_INERTIA[4] = {7.0f, 14.0f, 27.0f, 48.0f};
    constexpr float VERTICAL_WAVE_INERTIA[4] = {12.0f, 28.0f, 68.0f, 138.0f};
    constexpr float TIDE_RESPONSE[4] = {0.22f, 0.42f, 0.70f, 1.0f};

    // RGB565 调色板：避免连续亮青轮廓，主要依靠灰蓝明度差、碎裂浪纹和近黑水体塑造细节。
    constexpr uint16_t COLOR_SKY_TOP = 0x0041;       // #05090D 附近
    constexpr uint16_t COLOR_SKY_MID = 0x0883;       // #0B1118 附近
    constexpr uint16_t COLOR_SKY_HORIZON = 0x4AAB;   // 冷灰地平线
    constexpr uint16_t COLOR_CLOUD_DARK = 0x0863;
    constexpr uint16_t COLOR_CLOUD_MID = 0x1925;
    constexpr uint16_t COLOR_HAZE = 0x632D;
    constexpr uint16_t COLOR_WATER_BODY[4] = {0x21C9, 0x1187, 0x0926, 0x00C4};
    constexpr uint16_t COLOR_WATER_CAP[4] = {0x326C, 0x2A4B, 0x224A, 0x19E8};
    constexpr uint16_t COLOR_CREST_DIM = 0x63D0;
    constexpr uint16_t COLOR_CREST_MID = 0x8D15;
    constexpr uint16_t COLOR_FOAM = 0xADD8;
    constexpr uint16_t COLOR_GLINT = 0x2A8C;
    constexpr uint16_t COLOR_DEEP_GLINT = 0x19E8;

    float ClampFloat(float value, float low, float high)
    {
        if (value < low)
            return low;
        if (value > high)
            return high;
        return value;
    }

    float FilterToward(float current, float target, float dt, float response_hz)
    {
        const float alpha = ClampFloat(dt * response_hz, 0.0f, 1.0f);
        return current + (target - current) * alpha;
    }

    float SignedPow(float value, float exponent)
    {
        const float magnitude = powf(fabsf(value), exponent);
        return value < 0.0f ? -magnitude : magnitude;
    }

    uint16_t Blend565(uint16_t a, uint16_t b, float amount)
    {
        const float t = ClampFloat(amount, 0.0f, 1.0f);
        const int ar = (a >> 11) & 0x1F;
        const int ag = (a >> 5) & 0x3F;
        const int ab = a & 0x1F;
        const int br = (b >> 11) & 0x1F;
        const int bg = (b >> 5) & 0x3F;
        const int bb = b & 0x1F;
        const int r = ar + (int)lroundf((br - ar) * t);
        const int g = ag + (int)lroundf((bg - ag) * t);
        const int blue = ab + (int)lroundf((bb - ab) * t);
        return (uint16_t)((r << 11) | (g << 5) | blue);
    }
}

void UIFluidSurface::reset(int width, int height)
{
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    node_count_ = 0;
    filtered_roll_deg_ = 0.0f;
    filtered_roll_rate_dps_ = 0.0f;
    filtered_roll_accel_dps2_ = 0.0f;
    filtered_lateral_accel_g_ = 0.0f;
    filtered_vertical_accel_g_ = 0.0f;
    phase_seconds_ = 0.0f;
    tide_phase_seconds_ = 0.0f;
    motion_energy_ = 0.0f;
    edge_impact_energy_[0] = 0.0f;
    edge_impact_energy_[1] = 0.0f;
    edge_impact_y_[0] = 0.0f;
    edge_impact_y_[1] = 0.0f;

    for (int layer = 0; layer < LAYER_COUNT; ++layer)
    {
        for (int i = 0; i < MAX_NODES; ++i)
        {
            surface_y_[layer][i] = 0.0f;
            velocity_y_[layer][i] = 0.0f;
        }
    }
    for (int i = 0; i < MAX_NODES; ++i)
        next_surface_y_[i] = 0.0f;

    if (width_ == 0 || height_ == 0)
        return;

    node_count_ = (width_ + NODE_SPACING_PX - 1) / NODE_SPACING_PX + 1;
    if (node_count_ > MAX_NODES)
        node_count_ = MAX_NODES;

    for (int layer = 0; layer < LAYER_COUNT; ++layer)
    {
        const float base_y = baseSurfaceY(layer);
        for (int i = 0; i < node_count_; ++i)
        {
            // 每层使用不同空间频率，避免四条浪带以完全相同的形状同步移动。
            surface_y_[layer][i] = base_y +
                                           sinf((float)i * AUTO_WAVE_SPATIAL[layer] + layer * 1.37f) *
                                               AUTO_WAVE_AMPLITUDE_PX[layer];
        }
    }
}

float UIFluidSurface::baseSurfaceY(int layer) const
{
    if (layer < 0)
        layer = 0;
    if (layer >= LAYER_COUNT)
        layer = LAYER_COUNT - 1;
    return (float)height_ * BASE_HEIGHT_RATIO[layer];
}

float UIFluidSurface::normalizedNodeX(int index) const
{
    if (node_count_ <= 1)
        return 0.0f;
    return ((float)index / (float)(node_count_ - 1)) * 2.0f - 1.0f;
}

void UIFluidSurface::update(float dt_seconds, const UIFluidInput &input)
{
    if (node_count_ < 2)
        return;

    /*
     * TE 等待与其他主循环工作会让步长轻微抖动。上限用于丢弃后台停顿后的旧时间，
     * 防止显式积分一次跨过数百毫秒；下限避免连续同毫秒调用导致零步长。
     */
    const float dt = ClampFloat(dt_seconds, 0.005f, 0.040f);
    const float target_roll = input.valid ? ClampFloat(input.roll_deg, -MAX_ROLL_DEG, MAX_ROLL_DEG) : 0.0f;
    const float target_rate = input.valid ? ClampFloat(input.roll_rate_dps, -MAX_ROLL_RATE_DPS, MAX_ROLL_RATE_DPS) : 0.0f;
    const float target_angular_accel = input.valid
                                           ? ClampFloat(input.roll_accel_dps2, -MAX_ROLL_ACCEL_DPS2, MAX_ROLL_ACCEL_DPS2)
                                           : 0.0f;
    const float target_lateral = input.valid
                                     ? ClampFloat(input.lateral_accel_g, -MAX_LINEAR_ACCEL_G, MAX_LINEAR_ACCEL_G)
                                     : 0.0f;
    const float target_vertical = input.valid
                                      ? ClampFloat(input.vertical_accel_g, -MAX_LINEAR_ACCEL_G, MAX_LINEAR_ACCEL_G)
                                      : 0.0f;

    filtered_roll_deg_ = FilterToward(filtered_roll_deg_, target_roll, dt, ROLL_FILTER_HZ);
    filtered_roll_rate_dps_ = FilterToward(filtered_roll_rate_dps_, target_rate, dt, DYNAMIC_FILTER_HZ);
    filtered_roll_accel_dps2_ = FilterToward(filtered_roll_accel_dps2_, target_angular_accel, dt, DYNAMIC_FILTER_HZ);
    filtered_lateral_accel_g_ = FilterToward(filtered_lateral_accel_g_, target_lateral, dt, DYNAMIC_FILTER_HZ);
    filtered_vertical_accel_g_ = FilterToward(filtered_vertical_accel_g_, target_vertical, dt, DYNAMIC_FILTER_HZ);

    phase_seconds_ += dt;
    tide_phase_seconds_ += dt;
    if (tide_phase_seconds_ >= TIDE_PERIOD_SECONDS)
        tide_phase_seconds_ -= TIDE_PERIOD_SECONDS;

    /*
     * 运动能量只负责泡沫/碎浪密度，不直接改变水准面。上升快、衰减慢，使一次快速晃动结束后
     * 仍能看到短暂余波；静止噪声经归一化后不会长期维持高亮泡沫。
     */
    const float target_energy = ClampFloat(fabsf(target_rate) / 330.0f +
                                               fabsf(target_angular_accel) / 3000.0f +
                                               (fabsf(target_lateral) + fabsf(target_vertical)) * 0.65f,
                                           0.0f,
                                           1.6f);
    const float energy_response = target_energy > motion_energy_ ? 9.0f : 1.35f;
    motion_energy_ = FilterToward(motion_energy_, target_energy, dt, energy_response);

    const float roll_normalized = ClampFloat(filtered_roll_deg_ / MAX_ROLL_DEG, -1.0f, 1.0f);
    // 0.72 次幂放大小角度：约 5° 已能产生 45% 的最大斜率，满幅仍被限制在 ±15°。
    const float amplified_roll = SignedPow(roll_normalized, 0.72f);
    const float tide = sinf(tide_phase_seconds_ * (2.0f * PI_F / TIDE_PERIOD_SECONDS)) * TIDE_RANGE_PX;

    for (int layer = 0; layer < LAYER_COUNT; ++layer)
    {
        const float base_y = baseSurfaceY(layer);
        for (int i = 0; i < node_count_; ++i)
        {
            const int left = i > 0 ? i - 1 : i;
            const int right = i + 1 < node_count_ ? i + 1 : i;
            const float nx = normalizedNodeX(i);
            const float primary_wave = sinf(phase_seconds_ * AUTO_WAVE_SPEED[layer] +
                                             (float)i * AUTO_WAVE_SPATIAL[layer] + layer * 0.83f);
            const float secondary_wave = sinf(-phase_seconds_ * (AUTO_WAVE_SPEED[layer] * 0.57f) +
                                               (float)i * (AUTO_WAVE_SPATIAL[layer] * 0.47f) + layer * 1.91f);
            const float wind_wave = (primary_wave + secondary_wave * 0.46f) * AUTO_WAVE_AMPLITUDE_PX[layer];
            const float equilibrium_y = base_y + tide * TIDE_RESPONSE[layer] -
                                        nx * amplified_roll * SLOPE_HALF_RANGE_PX[layer] + wind_wave;
            const float laplacian = surface_y_[layer][left] + surface_y_[layer][right] -
                                    2.0f * surface_y_[layer][i];

            float acceleration = (equilibrium_y - surface_y_[layer][i]) * RESTORE_STRENGTH[layer];
            acceleration += laplacian * NEIGHBOR_STRENGTH[layer];
            acceleration -= velocity_y_[layer][i] * VELOCITY_DAMPING[layer];

            // 转速与角加速度方向相反地推拉两端；停止转动时角加速度仍会制造一次反向回摆。
            acceleration -= nx * filtered_roll_rate_dps_ * RATE_INERTIA[layer];
            acceleration -= nx * filtered_roll_accel_dps2_ * ANGULAR_ACCEL_INERTIA[layer];

            // 去重力线性加速度不改变长期水准面，只作为短时的横向冲击与整体抬升/下压输入。
            acceleration -= nx * filtered_lateral_accel_g_ * LINEAR_ACCEL_INERTIA[layer];
            acceleration += filtered_vertical_accel_g_ * VERTICAL_ACCEL_INERTIA[layer];

            /*
             * 上下摇晃若只给所有节点相同加速度，视觉上只会整片平移。这里用两组不同空间频率
             * 把同一份 Z 轴线性加速度拆成局部高低差：远海保持克制，近景形成可读但不过度的波浪。
             */
            const float vertical_ripple = sinf((float)i * (0.19f + layer * 0.025f) +
                                                phase_seconds_ * (1.25f + layer * 0.12f)) +
                                          sinf((float)i * 0.37f - phase_seconds_ * 0.83f + layer) * 0.46f;
            acceleration += filtered_vertical_accel_g_ * vertical_ripple * VERTICAL_WAVE_INERTIA[layer];

            velocity_y_[layer][i] += acceleration * dt;
            velocity_y_[layer][i] = ClampFloat(velocity_y_[layer][i],
                                                -MAX_SURFACE_SPEED_PX_S,
                                                MAX_SURFACE_SPEED_PX_S);
            next_surface_y_[i] = surface_y_[layer][i] + velocity_y_[layer][i] * dt;

            const float travel_limit = 10.0f + layer * 5.0f;
            next_surface_y_[i] = ClampFloat(next_surface_y_[i],
                                             base_y - SLOPE_HALF_RANGE_PX[layer] - travel_limit,
                                             base_y + SLOPE_HALF_RANGE_PX[layer] + travel_limit);
        }

        // 两端吸收反射，近景保留更多回浪，远景更快恢复为稳定的海平线。
        const float edge_absorption = 0.76f + layer * 0.035f;
        velocity_y_[layer][0] *= edge_absorption;
        velocity_y_[layer][node_count_ - 1] *= edge_absorption;

        for (int i = 0; i < node_count_; ++i)
            surface_y_[layer][i] = next_surface_y_[i];
    }

    applyEdgeCollisions(dt);
}

void UIFluidSurface::applyEdgeCollisions(float dt_seconds)
{
    /*
     * 屏幕左右边缘在视觉上充当容器壁。只有前景浪头已经明显抬高且仍在快速向上运动时才算碰撞，
     * 因此缓慢潮汐、自然小浪和倾斜后静止的水面不会反复触发闪烁。
     */
    for (int side = 0; side < 2; ++side)
    {
        edge_impact_energy_[side] = max(0.0f,
                                        edge_impact_energy_[side] -
                                            dt_seconds * EDGE_COLLISION_DECAY_PER_SECOND);

        const int edge_index = side == 0 ? 0 : node_count_ - 1;
        const int inner_index = side == 0 ? 1 : node_count_ - 2;
        const float rise_px = baseSurfaceY(LAYER_COUNT - 1) -
                              surface_y_[LAYER_COUNT - 1][edge_index];
        const float upward_speed = max(0.0f, -velocity_y_[LAYER_COUNT - 1][edge_index]);
        if (rise_px < EDGE_COLLISION_MIN_RISE_PX ||
            upward_speed < EDGE_COLLISION_MIN_SPEED_PX_S)
        {
            continue;
        }

        const float impact_energy = ClampFloat((rise_px - EDGE_COLLISION_MIN_RISE_PX) / 28.0f +
                                                   (upward_speed - EDGE_COLLISION_MIN_SPEED_PX_S) / 95.0f +
                                                   motion_energy_ * 0.22f,
                                               0.20f,
                                               1.55f);
        edge_impact_energy_[side] = max(edge_impact_energy_[side], impact_energy);
        edge_impact_y_[side] = surface_y_[LAYER_COUNT - 1][edge_index];

        /*
         * 把向上的边缘速度翻转为较弱的向下速度，并把一部分能量传给内侧节点。
         * 这样碰撞后会形成可见的反向回浪，而不是只在原轨迹上叠加一次泡沫贴图。
         */
        const float rebound_speed = upward_speed * (0.34f + min(impact_energy, 1.0f) * 0.10f);
        velocity_y_[LAYER_COUNT - 1][edge_index] = rebound_speed;
        velocity_y_[LAYER_COUNT - 1][inner_index] += rebound_speed * 0.24f;
        surface_y_[LAYER_COUNT - 1][edge_index] += min(2.4f, impact_energy * 1.6f);
    }
}

int UIFluidSurface::surfacePixelAtNode(int layer, int index) const
{
    if (node_count_ == 0)
        return height_;
    layer = constrain(layer, 0, LAYER_COUNT - 1);
    index = constrain(index, 0, node_count_ - 1);
    return constrain((int)lroundf(surface_y_[layer][index]), 0, height_ - 1);
}

int UIFluidSurface::surfacePixelAtX(int layer, int x) const
{
    if (node_count_ == 0)
        return height_;
    return surfacePixelAtNode(layer, x / NODE_SPACING_PX);
}

void UIFluidSurface::drawStormSky() const
{
    const int horizon_y = constrain((int)lroundf(baseSurfaceY(0)), 8, height_ - 1);

    // 逐行渐变只覆盖约 1/4 屏高，成本有限；比两块纯色色带更接近参考图中的压低风暴天空。
    for (int y = 0; y <= horizon_y; ++y)
    {
        const float t = horizon_y > 0 ? (float)y / (float)horizon_y : 1.0f;
        const uint16_t color = t < 0.68f
                                   ? Blend565(COLOR_SKY_TOP, COLOR_SKY_MID, t / 0.68f)
                                   : Blend565(COLOR_SKY_MID, COLOR_SKY_HORIZON, (t - 0.68f) / 0.32f);
        HAL_Fill_Rect(0, y, width_, 1, color);
    }

    /*
     * 云层由低对比度的长短断片组成，且只以极慢速度漂移。这里避免圆形云朵和高饱和描边，
     * 在 428×142 分辨率上保留真实风暴云的横向压迫感而不显得卡通。
     */
    const int drift = (int)lroundf(phase_seconds_ * 1.7f);
    for (int cloud = 0; cloud < 19; ++cloud)
    {
        const int band = cloud % 5;
        const int span = width_ + 90;
        int x = (cloud * 79 + band * 31 + drift * (1 + band / 2)) % span - 45;
        const int y = 4 + band * 5 + (cloud % 3);
        const int w = 24 + (cloud * 17) % 58;
        const int h = 1 + ((cloud + band) % 3 == 0 ? 2 : 1);
        const uint16_t color = band < 2 ? COLOR_CLOUD_DARK : COLOR_CLOUD_MID;
        HAL_Fill_Rect(x, y, w, h, color);
        if ((cloud % 3) == 0)
            HAL_Draw_Line(x + w / 4, y + h, x + w + 9, y + h + 1, COLOR_CLOUD_DARK);
    }

    // 冷灰薄雾将天空与远海分开，不使用一整条高亮描边。
    for (int x = 0; x < width_; x += 37)
    {
        const int segment = 16 + (x * 7) % 25;
        HAL_Draw_Line(x, horizon_y - 1, min(width_ - 1, x + segment), horizon_y - 1, COLOR_HAZE);
    }
}

void UIFluidSurface::drawWaveLayer(int layer) const
{
    const uint16_t body = COLOR_WATER_BODY[layer];
    const uint16_t cap = COLOR_WATER_CAP[layer];
    const int phase_bucket = (int)(phase_seconds_ * (2.0f + layer * 0.7f));

    // 每层先按 4px 竖条填满到底，随后更近的浪带覆盖下部，天然形成从地平线到前景的遮挡关系。
    for (int i = 0; i < node_count_; ++i)
    {
        const int x = min(width_ - 1, i * NODE_SPACING_PX);
        const int strip_w = min(NODE_SPACING_PX, width_ - x);
        const int y = surfacePixelAtNode(layer, i);
        if (strip_w <= 0 || y >= height_)
            continue;

        HAL_Fill_Rect(x, y, strip_w, height_ - y, body);
        const int cap_h = 1 + (layer >= 2 ? 1 : 0);
        HAL_Fill_Rect(x, y, strip_w, min(cap_h, height_ - y), cap);
    }

    // 水面高光故意断裂；相位桶缓慢移动缺口，避免 V1 那条连续亮青轮廓造成“卡通水槽”观感。
    for (int i = 1; i < node_count_; ++i)
    {
        if (((i + phase_bucket + layer * 3) % (6 - min(layer, 2))) == 0)
            continue;
        const int x0 = min(width_ - 1, (i - 1) * NODE_SPACING_PX);
        const int x1 = min(width_ - 1, i * NODE_SPACING_PX);
        const int y0 = surfacePixelAtNode(layer, i - 1);
        const int y1 = surfacePixelAtNode(layer, i);
        HAL_Draw_Line(x0, y0, x1, y1, layer < 2 ? COLOR_CREST_DIM : COLOR_CREST_MID);
    }
}

void UIFluidSurface::drawWaterDetails() const
{
    const int horizon_y = surfacePixelAtX(0, width_ / 2);
    const int water_span = max(18, height_ - horizon_y - 5);
    const int travel = (int)lroundf(phase_seconds_ * 7.0f);

    /*
     * 水体内部用不同长度、速度和深度的断裂浪纹补充细节。所有位置均解析生成，不维护粒子堆；
     * 远处短而密、近处长且对比度更高，模拟参考图中的透视波纹和潮水纹理。
     */
    for (int streak = 0; streak < 58; ++streak)
    {
        const int direction = (streak & 1) ? 1 : -1;
        const int span = width_ + 48;
        int x = (streak * 73 + direction * travel * (1 + streak % 3)) % span;
        if (x < 0)
            x += span;
        x -= 24;

        const int depth = 5 + (streak * 29) % water_span;
        const int y = horizon_y + depth +
                      (int)lroundf(sinf(phase_seconds_ * (0.35f + (streak % 4) * 0.09f) + streak) * 2.0f);
        const int length = 4 + (depth * 13 + streak * 5) % (8 + max(5, depth / 3));
        const int slope = ((streak * 11) % 5) - 2;
        const uint16_t color = depth < water_span / 3
                                   ? COLOR_GLINT
                                   : (streak % 5 == 0 ? COLOR_CREST_DIM : COLOR_DEEP_GLINT);
        HAL_Draw_Line(x, y, min(width_ - 1, x + length), y + slope, color);
        if (depth > water_span * 2 / 3 && streak % 7 == 0)
            HAL_Draw_Line(x + length / 3, y + 2, min(width_ - 1, x + length + 5), y + 1, COLOR_GLINT);
    }

    // 中近景曲率和速度较大时生成碎浪；运动能量越高，阈值越低、亮色段越多。
    for (int layer = 2; layer < LAYER_COUNT; ++layer)
    {
        const float curvature_threshold = layer == 2 ? 0.70f : 0.95f;
        const float velocity_threshold = (layer == 2 ? 31.0f : 37.0f) - motion_energy_ * 12.0f;
        const int phase_bucket = (int)(phase_seconds_ * 9.0f);
        for (int i = 1; i + 1 < node_count_; ++i)
        {
            const float curvature = fabsf(surface_y_[layer][i - 1] + surface_y_[layer][i + 1] -
                                          2.0f * surface_y_[layer][i]);
            const bool active_crest = curvature > curvature_threshold ||
                                      fabsf(velocity_y_[layer][i]) > velocity_threshold;
            const bool natural_breaker = layer == 3 && ((i + phase_bucket) % 13 == 0);
            if ((!active_crest && !natural_breaker) || ((i + phase_bucket) % 3 == 0))
                continue;

            const int x = min(width_ - 1, i * NODE_SPACING_PX);
            const int y = surfacePixelAtNode(layer, i) - 1;
            const int foam_length = 3 + (i * 7 + phase_bucket) % (layer == 3 ? 10 : 7);
            const uint16_t foam_color = (motion_energy_ > 0.62f || (i % 5 == 0)) ? COLOR_FOAM : COLOR_CREST_DIM;
            HAL_Draw_Line(x - 1, y, min(width_ - 1, x + foam_length), y - (i % 3 == 0 ? 1 : 0), foam_color);

            // 强烈冲击才出现少量飞沫，避免静止时满屏白点造成噪声感。
            if (motion_energy_ > 0.85f && layer == 3 && (i + phase_bucket) % 7 == 0)
            {
                const int spray_height = 1 + (i + phase_bucket) % 4;
                HAL_Draw_Line(x + 1, y - 1, x + 1 + (i & 1), y - spray_height, COLOR_FOAM);
            }
        }
    }

    // 前景潮水底部保留少量横向泡沫带，强化“浪正在向观察者推进”的感觉。
    const int tide_shift = (int)lroundf(sinf(tide_phase_seconds_ * (2.0f * PI_F / TIDE_PERIOD_SECONDS)) * 9.0f);
    for (int band = 0; band < 7; ++band)
    {
        const int x = (band * 71 + travel * 2) % (width_ + 30) - 15;
        const int y = height_ - 7 - ((band * 11 + tide_shift + 18) % 18);
        const int length = 14 + (band * 17) % 34;
        HAL_Draw_Line(x, y, min(width_ - 1, x + length), y - (band & 1),
                      band % 3 == 0 ? COLOR_CREST_MID : COLOR_CREST_DIM);
    }
}

void UIFluidSurface::drawEdgeCollisions() const
{
    /*
     * 边缘碰撞绘制在普通浪纹之后：一条贴边上冲、两条向内破碎的泡沫和少量离散飞沫共同表达撞击。
     * 亮白只用于强碰撞，较弱碰撞使用灰蓝色，避免左右两侧长期出现卡通化的白色边框。
     */
    const int phase_bucket = (int)(phase_seconds_ * 23.0f);
    for (int side = 0; side < 2; ++side)
    {
        const float energy = edge_impact_energy_[side];
        if (energy < 0.04f)
            continue;

        const int direction = side == 0 ? 1 : -1;
        const int wall_x = side == 0 ? 0 : width_ - 1;
        const int y = constrain((int)lroundf(edge_impact_y_[side]), 4, height_ - 3);
        const int reach = 5 + (int)lroundf(energy * 13.0f);
        const int spray_height = 3 + (int)lroundf(energy * 8.0f);
        const uint16_t strong_color = energy > 0.72f ? COLOR_FOAM : COLOR_CREST_MID;

        HAL_Draw_Line(wall_x, y,
                      constrain(wall_x + direction * reach, 0, width_ - 1), y - 1,
                      strong_color);
        HAL_Draw_Line(wall_x, y + 2,
                      constrain(wall_x + direction * (reach * 2 / 3), 0, width_ - 1), y + 2,
                      COLOR_CREST_DIM);
        HAL_Draw_Line(wall_x, y,
                      wall_x, max(0, y - spray_height),
                      strong_color);

        const int particle_count = 2 + (int)lroundf(min(energy, 1.2f) * 3.0f);
        for (int particle = 0; particle < particle_count; ++particle)
        {
            const int inward = 2 + particle * 3 + ((phase_bucket + particle * 5) % 3);
            const int particle_x = constrain(wall_x + direction * inward, 0, width_ - 1);
            const int particle_y = constrain(y - 2 -
                                                 ((particle * 5 + phase_bucket) % max(2, spray_height)),
                                             0,
                                             height_ - 1);
            HAL_Draw_Pixel(particle_x, particle_y,
                           particle % 3 == 0 ? strong_color : COLOR_CREST_DIM);
        }
    }
}

void UIFluidSurface::draw() const
{
    if (node_count_ < 2 || width_ <= 0 || height_ <= 0)
        return;

    drawStormSky();
    for (int layer = 0; layer < LAYER_COUNT; ++layer)
        drawWaveLayer(layer);
    drawWaterDetails();
    drawEdgeCollisions();
}
