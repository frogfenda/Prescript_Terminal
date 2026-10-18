/*
【模块职责】编辑滚轮与步骤导轨渲染：宽度感知排布、连续阻尼、水平双红绳、弧面刻度和薄读数窗。
【布局】顶部步骤、双绳导轨、中部弧面、底部提示各自留白；原字体不缩放，靠位置和亮度表达纵深。
【线程与资源】全部在主循环当前 Sprite 上绘制；固定上限采样，不访问文件、IMU 或业务配置。
*/
#include "ui/ui_animators.h"
#include "ui/ui_dial_geometry.h"
#include "ui/ui_frame.h"
#include "ui/ui_menu_motion.h"
#include "ui/ui_text.h"
#include <stdio.h>

namespace {
uint16_t Shade(uint16_t color, float strength)
{
    strength = fmaxf(0.0f, fminf(1.0f, strength));
    return (uint16_t)((uint16_t)(((color >> 11) & 31) * strength) << 11) |
           (uint16_t)((uint16_t)(((color >> 5) & 63) * strength) << 5) |
           (uint16_t)((color & 31) * strength);
}

// 保留原有 16ms 绘制节奏，积分使用真实时间；长时间离页不把旧动量跨秒推进。
bool FrameStep(uint32_t &last_tick, float &dt)
{
    const uint32_t now = millis();
    const uint32_t elapsed = now - last_tick;
    if (elapsed < UITheme::FRAME_FAST_MS) return false;
    last_tick = now;
    dt = min(elapsed, (uint32_t)100) * 0.001f;
    return true;
}

int Wrap(int value, int minimum, int maximum)
{
    const int range = maximum - minimum + 1;
    int result = (value - minimum) % range;
    if (result < 0) result += range;
    return minimum + result;
}
}

void DialAnimator::reset()
{
    offset_ = velocity_ = reflection_ = 0.0f;
    last_tick_ = millis();
}

void DialAnimator::trigger(int delta)
{
    if (delta == 0) return;
    if (offset_ == 0.0f && velocity_ == 0.0f) last_tick_ = millis();
    offset_ += delta;
}

bool DialAnimator::update()
{
    float dt;
    if (!FrameStep(last_tick_, dt)) return false;
    bool changed = UI_MenuAdvanceDamped(0.0f, dt, UITheme::Dial::MotionResponse,
                                       offset_, velocity_);
    changed |= UI_MenuApproach(window_target_, dt, UITheme::Dial::LayoutResponse, window_width_);
    const float light = -velocity_ / (fabsf(velocity_) + 12.0f);
    changed |= UI_MenuApproach(light, dt, 12.0f, reflection_);
    return changed;
}

void DialAnimator::drawNumberDial(int y, int current, int minimum, int maximum, const char *suffix)
{
    drawDial(y, current, minimum, maximum, nullptr, suffix);
}

void DialAnimator::drawStringDial(int y, int current, const char **names, int count)
{
    if (!names || count <= 0) return;
    drawDial(y, current, 0, count - 1, names, "");
}

void DialAnimator::drawDial(int y, int current, int minimum, int maximum,
                            const char **names, const char *suffix)
{
    if (maximum < minimum) return;
    const int sw = HAL_Get_Screen_Width();
    const int cx = sw / 2;
    const int margin = UITheme::SafeMarginX();
    const int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
    const float half_span = cx - margin;
    int maximum_width = 0;
    char number[24];
    if (names)
    {
        for (int i = minimum; i <= maximum; ++i)
            maximum_width = max(maximum_width, UIText::Measure(names[i]));
    }
    else
    {
        // 两端和零覆盖当前各编辑字段的最长数字形态（包括负号），不会随进位改变节距。
        const int samples[] = {minimum, maximum, 0};
        for (const int value : samples)
        {
            snprintf(number, sizeof(number), "%02d", value);
            maximum_width = max(maximum_width, UIText::Measure(number));
        }
    }
    const int suffix_width = suffix ? UIText::Measure(suffix) : 0;
    const float pitch = UIDialGeometry::Pitch(maximum_width, sw);
    window_target_ = min((float)(sw - 2*margin - 12), (float)max(48, maximum_width + 20));
    if (window_width_ == 0.0f) window_width_ = window_target_;
    const int current_value = Wrap(current, minimum, maximum);
    if (!names) snprintf(number, sizeof(number), "%02d", current_value);
    const int current_width = UIText::Measure(names ? names[current_value] : number);
    // 切换到较长字段的首帧也保证字形处在窗内，剩余边距继续平滑追随目标宽度。
    const int window_w = max((int)lroundf(window_width_), current_width + 12);
    const float lift = UITheme::Dial::ArcLift();
    const int rail_y = y + line_h + UITheme::Dial::RailGap();

    // 弧面轨道放在字形下方，近处更亮；刻度与数字共用连续槽位，快滚时也不会贴图漂移。
    for (int x = margin; x < sw - margin; x += 3)
    {
        const float depth = UIDialGeometry::Depth(x - cx, half_span);
        const int yy = rail_y - (int)lroundf(lift * depth);
        HAL_Draw_Line(x, yy, min(x + 2, sw - margin - 1), yy,
                      Shade(TFT_CYAN, 0.10f + 0.12f*(1.0f-depth)));
    }
    const int base = -(int)lroundf(offset_);
    for (int i = base - 8; i <= base + 8; ++i)
    {
        const float slot = i + offset_;
        if (fabsf(slot) > UIDialGeometry::MaxSlots) continue;
        const float x = UIDialGeometry::X(slot, pitch);
        const float brightness = UIDialGeometry::Brightness(slot, x, half_span, maximum_width * 0.5f);
        if (brightness < 0.025f) continue;
        const int xx = cx + (int)lroundf(x);
        const int yy = rail_y - (int)lroundf(lift * UIDialGeometry::Depth(x, half_span));
        HAL_Draw_Line(xx, yy - 2, xx, yy + 2, Shade(TFT_CYAN, brightness * 0.55f));
    }
    // 读数窗沿用菜单薄侧面；单位放在右上侧的留白带，不挡住从右侧进入的数字。
    UIFrame::DrawFloatingSelection(cx - window_w/2, y - 6, window_w,
                                   line_h + 12, reflection_);
    for (int i = base - 8; i <= base + 8; ++i)
    {
        const float slot = i + offset_;
        if (fabsf(slot) > UIDialGeometry::MaxSlots) continue;
        const float x = UIDialGeometry::X(slot, pitch);
        const int value = Wrap(current + i, minimum, maximum);
        const char *text;
        if (names) text = names[value];
        else { snprintf(number, sizeof(number), "%02d", value); text = number; }
        const int width = UIText::Measure(text);
        const float brightness = UIDialGeometry::Brightness(slot, x, half_span, width*0.5f);
        if (brightness < 0.025f) continue;
        const int xx = cx + (int)lroundf(x) - width/2;
        const int yy = y - (int)lroundf(lift * UIDialGeometry::Depth(x, half_span));
        UIText::DrawClippedColor(xx, yy, text, Shade(TFT_CYAN, brightness), margin, sw - margin);
    }
    if (suffix_width)
        UIText::DrawClippedColor(cx + window_w/2 + 8, y - line_h - 4, suffix,
                                 Shade(TFT_CYAN, 0.65f), margin, sw - margin);
}

void TacticalLinkEngine::reset(int phase)
{
    jumpTo(phase);
    dial_remaining_ = dial_velocity_ = rope_travel_ = 0.0f;
    last_tick_ = millis();
    rail_ropes_.reset();
    // 进入时间仅用于独立的视觉种子；同一帧反复 draw 不会重新抽样或改变绳形。
    rail_ropes_.setVariation(millis() ^ ((uint32_t)(phase+1)*0x9e3779b9U), UITheme::Link::RopeVariation);
    rail_frame_gate_.reset();
    redraw_frame_gate_.reset();
    redraw_pending_ = false;
    rail_redraw_pending_ = false;
    drawn_step_count_ = 0;
}

void TacticalLinkEngine::jumpTo(int phase)
{
    position_ = (float)phase;
    target_ = phase;
    velocity_ = 0.0f;
}

void TacticalLinkEngine::triggerDial(int delta)
{
    // 与数字接收同一份增量，但用稍慢的阻尼模拟松绳；新输入和反向都保留原速度。
    dial_remaining_ += delta;
}

bool TacticalLinkEngine::update(int current_phase)
{
    // 删除/覆盖确认页没有步骤导轨，不能因后台绳子持续触发整屏刷新。
    if (current_phase < 0 || current_phase >= drawn_step_count_) return false;
    // 在 16ms 动画门控之前锁存闲置节拍，避免 33ms 被固定取整成 48ms。
    rail_redraw_pending_ |= rail_frame_gate_.due(UITheme::FRAME_NORMAL_MS);
    target_ = current_phase;
    float dt;
    if (!FrameStep(last_tick_, dt)) return false;
    bool changed = UI_MenuAdvanceDamped((float)target_, dt, UITheme::Link::MotionResponse,
                                        position_, velocity_);
    // 数字增大时从右侧滚入，绳形也向左牵动。累计位移仅取周期余数，长时间操作不损失浮点精度。
    // 牵引与步骤位置完全独立；换字段的 dialAnim.reset 不会打断这根绳子剩余的惯性。
    const float remaining_before = dial_remaining_;
    changed |= UI_MenuAdvanceDamped(0.0f, dt, UITheme::Link::RopeResponse,
                                    dial_remaining_, dial_velocity_);
    rope_travel_ = fmodf(rope_travel_ + (remaining_before-dial_remaining_)*UITheme::Link::RopeTravelPerStep, 1.0f);
    rail_ropes_.update(-dial_velocity_, dt);
    changed |= rail_redraw_pending_;
    rail_redraw_pending_ = false;
    return changed;
}

bool TacticalLinkEngine::redrawDue(bool content_changed)
{
    // 红绳一直缓慢运动，数字则在首格输入时重新计时；两个更新门控可能错开几毫秒。
    // 用一个独立的整页绘制节拍合并它们，否则即使删掉 onKnob 绘制也可能交替推屏至约 120FPS。
    redraw_pending_ |= content_changed;
    if (!redraw_pending_ || !redraw_frame_gate_.due(UITheme::FRAME_FAST_MS)) return false;
    redraw_pending_ = false;
    return true;
}

void TacticalLinkEngine::draw(int y, const char **names, int count, int current_phase, int spacing)
{
    if (!names || count <= 0) return;
    redraw_frame_gate_.reset();
    redraw_pending_ = false;
    drawn_step_count_ = count;
    const int sw = HAL_Get_Screen_Width();
    const int margin = UITheme::SafeMarginX();
    const int cx = sw/2;
    const int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
    const int rail_y = y + line_h + UITheme::Link::RailGap();
    const int gap = constrain(spacing / 4, 24, 32);
    const int active = constrain(current_phase, 0, count - 1);
    const float position = fmaxf(0.0f, fminf((float)(count - 1), position_));
    const int left = (int)floorf(position);
    float node = 0.0f;
    int previous_width = 0;
    // 只累计实际文字宽度；无固定节点数组，因此未来增加步骤不会越界或依赖堆内存。
    for (int i = 0; i <= left; ++i)
    {
        const int width = UIText::Measure(names[i]);
        if (i) node += (previous_width + width)*0.5f + gap;
        previous_width = width;
    }
    float anchor = node;
    if (left + 1 < count)
        anchor += (position-left) * ((previous_width + UIText::Measure(names[left+1]))*0.5f + gap);

    // 控制点始终处于同一水平线上；直接复用主菜单默认双股、摆幅和深度遮挡，仅将路径水平展开。
    // 六个区间覆盖导轨，内部控制点横向游移；波幅不变，但波峰间距和两股节奏各自缓慢变化。
    UIMenuRopeAnchor rail_anchors[9];
    const float material_offset = rope_travel_ - rail_ropes_.followOffset()*0.5f;
    rail_ropes_.horizontalAnchors(rail_anchors, 9, margin, sw-margin, (float)rail_y,
                                  UITheme::Link::RopeCycles, material_offset);
    rail_ropes_.draw(rail_anchors, 9, material_offset, margin, sw-margin);
    node = 0.0f;
    previous_width = 0;
    for (int i = 0; i < count; ++i)
    {
        const int width = UIText::Measure(names[i]);
        if (i) node += (previous_width + width)*0.5f + gap;
        previous_width = width;
        const float dx = node - anchor;
        const int xx = cx + (int)lroundf(dx);
        const float focus = expf(-0.9f*(i-position)*(i-position));
        const float edge = fmaxf(0.0f, fminf(1.0f, ((cx-margin)-fabsf(dx))/45.0f));
        const float brightness = (0.22f + (i < active ? 0.18f : 0.0f) + 0.70f*focus)*edge;
        if (brightness > 0.02f)
            UIText::DrawClippedColor(xx-width/2, y, names[i], Shade(TFT_CYAN, brightness), margin, sw-margin);
        if (xx < margin || xx >= sw-margin) continue;
        if (i == active)
        {
            // 标记沿同一股绳子运动，避免交叉时在两股之间切换造成上下跳动。
            // 控制点已非等间距；以屏幕 x 反求连续参数，使步骤标记仍准确贴在同一股绳上。
            const float parameter = rail_ropes_.horizontalParameterAtX(rail_anchors,9,(float)xx);
            const int marker_y = (int)lroundf(rail_ropes_.sample(rail_anchors, 9, parameter, 0).y);
            HAL_Draw_Line(xx-2, marker_y, xx+2, marker_y, TFT_CYAN);
        }
    }
}
