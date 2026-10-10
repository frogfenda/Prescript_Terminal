/*
【模块职责】横向指令纸带菜单。拥有独立的连续位置、速度和帧时钟。
【约束】仅主循环使用；菜单标题、条目和编辑行为由页面提供，不读取文件或改变路由。
纸带用矩形、阴影和折角表现深度，正文保持原始字体，避免点阵文字透视后失真。
*/
#pragma once
#include "ui/ui_menu_renderer.h"

class UIPaperMenuRenderer final : public UIMenuRenderer
{
    float position_ = 0.0f;
    float velocity_ = 0.0f;
    uint32_t last_tick_ = 0;
public:
    void reset(float position) override;
    bool update(float target, uint32_t now) override;
    float position() const override { return position_; }
    void draw(AppMenuBase &menu, float visual_position) override;
};
