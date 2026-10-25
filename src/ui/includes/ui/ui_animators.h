/*
【模块职责】共用编辑页的横向弧面滚轮和顶部步骤导轨；只维护视觉状态，不修改字段或保存配置。
【调用关系】App 调值时只 trigger()；主循环 update() 后经 redrawDue() 合并刷新，draw() 只绘制当前 Sprite。
【约束】主循环调用；使用原有统一字体与 HAL；不分配绘图缓冲、不创建任务、不自行推屏。
*/
#pragma once
#include <Arduino.h>
#include "ui/ui_frame.h"
#include "ui/ui_clock.h"
#include "ui/ui_menu_ropes.h"

class DialAnimator {
private:
    float offset_ = 0.0f;
    float velocity_ = 0.0f;
    float window_width_ = 0.0f;
    float window_target_ = 0.0f;
    float reflection_ = 0.0f;
    uint32_t last_tick_ = 0;
    void drawDial(int y, int current, int minimum, int maximum,
                  const char **names, const char *suffix);
public:
    /** 页面重新进入或更换编辑字段时清除滚动动量；保留读数窗宽度，以便连续适配下一字段。 */
    void reset();
    /** 数值已由 App 调整后传入同一个 delta；保留上一次速度，支持连续输入与中途反向。 */
    void trigger(int delta);
    /** 按实际帧间隔推进阻尼和读数窗；返回 true 时页面需要重绘，静止后停止刷新。 */
    bool update();
    /** y 为中心读数的文字顶边；范围由 App 决定，组件只循环展示，suffix 固定在读数窗右上侧。 */
    void drawNumberDial(int y, int current, int minimum, int maximum, const char *suffix = "");
    /** 字符串选项和数字共用几何、阻尼与读数窗；count<=0 安全跳过，原字符串只在本次调用内读取。 */
    void drawStringDial(int y, int current, const char **names, int count);
};

class TacticalLinkEngine {
private:
    float position_ = 0.0f;
    float velocity_ = 0.0f;
    int target_ = 0;
    uint32_t last_tick_ = 0;
    UIMenuRopes rail_ropes_;
    UIFrameGate rail_frame_gate_;
    UIFrameGate redraw_frame_gate_;
    bool redraw_pending_ = false;
    bool rail_redraw_pending_ = false;
    int drawn_step_count_ = 0;
    float dial_remaining_ = 0.0f;
    float dial_velocity_ = 0.0f;
    float rope_travel_ = 0.0f;
public:
    /** 仅在页面进入时调用，定位步骤并清空红绳牵引、闲置时钟和种子。 */
    void reset(int phase);
    /** 外部强制定位步骤；保留红绳的闲置相位和旋钮牵引，不因字段切换重新编绳。 */
    void jumpTo(int phase);
    /** App 调整当前字段并调用 dialAnim.trigger 后传入同一 delta；只记录视觉牵引，不修改业务值。 */
    void triggerDial(int delta);
    /** 实际帧间隔阻尼；切换步骤保留速度，静止时为红绳请求约 30FPS；越出步骤范围的确认页停止闲置刷新。 */
    bool update(int current_phase);
    /**
     * onLoop 在滚轮/导轨更新后传入合并的变化标记，统一限制整页最多每 16ms 绘制一次。
     * 两组件的积分时钟可以不同步；未到帧时保留待绘标记，最后一次输入不会被丢弃。
     * draw() 会同步清除待绘并重置节拍，使按键/进入页立即绘制后不紧跟第二次刷新。
     */
    bool redrawDue(bool content_changed);
    /**
     * y 为标签顶边；实际节点间距由相邻文字宽度和留白共同计算。
     * spacing 保留旧调用兼容，只作为留白偏好；水平双绳导轨兼作分隔线，摆幅复用主菜单默认值。
     * 超出屏幕的标签按完整 UTF-8 字形裁剪；不清屏、不推屏。
     */
    void draw(int y, const char **names, int count, int current_phase, int spacing = 120);
    /** 运行态紧凑导轨：保留红绳和状态标签，只显示一个状态，不展示设置流程。 */
    void drawCompact(int y, const char *label, int spacing = 85);
};
