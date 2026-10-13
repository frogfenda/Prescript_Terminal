/*
【模块职责】菜单渲染器注册表、滚轮兼容渲染器和纸带渲染器的样式选择。

第一阶段保留 AppMenuBase 原有绘制实现，先把“页面请求绘制”和“具体样式选择”
分开。这样新增第二套菜单时可以直接增加一个独立 UIMenuRenderer，而不必改动
旋钮、按键、陀螺仪事件和菜单条目业务代码。
*/
#include "ui/ui_menu_renderer.h"
#include "apps/app_menu_base.h"
#include "ui/ui_paper_menu.h"

namespace
{
    UIWheelMenuRenderer g_wheel_menu_renderer;
    UIPaperMenuRenderer g_paper_menu_renderer;
    UIMenuRenderStyle g_menu_style = UIMenuRenderStyle::Wheel;
    UIMenuFrameTiming g_frame_timing;
    uint32_t g_last_frame_start_us = 0;
}

UIMenuRenderer &UI_MenuRendererForStyle(UIMenuRenderStyle style)
{
    switch (style)
    {
    case UIMenuRenderStyle::PaperTape:
        return g_paper_menu_renderer;
    case UIMenuRenderStyle::Wheel:
    default:
        return g_wheel_menu_renderer;
    }
}

UIMenuRenderStyle UI_GetMenuRenderStyle() { return g_menu_style; }
void UI_SetMenuRenderStyle(UIMenuRenderStyle style)
{
    // 未注册值回退到滚轮，不让页面使用不完整的动画契约。
    g_menu_style = style == UIMenuRenderStyle::PaperTape ? style : UIMenuRenderStyle::Wheel;
}

void UIWheelMenuRenderer::draw(AppMenuBase &menu, float visual_position)
{
    // 当前滚轮布局仍由 AppMenuBase 的兼容实现提供，确保本阶段画面和操作完全不变。
    // 后续渲染器可以直接读取同一菜单页面状态，实现全新的整屏布局。
    menu.drawWheelMenuUI(visual_position);
}

UIMenuFrameTiming UI_GetMenuFrameTiming() { return g_frame_timing; }
void UI_RecordMenuFrameTiming(uint32_t frame_start_us, uint32_t draw_us, uint32_t push_us)
{
    // 单一主循环使用；记录整数耗时，不申请内存，也不在每帧输出日志。
    const uint32_t interval = frame_start_us - g_last_frame_start_us;
    g_frame_timing.interval_us = g_frame_timing.frames && interval <= 250000U ? interval : 0;
    g_last_frame_start_us = frame_start_us;
    ++g_frame_timing.frames;
    g_frame_timing.draw_us = draw_us;
    g_frame_timing.push_us = push_us;
    g_frame_timing.max_draw_us = max(g_frame_timing.max_draw_us, draw_us);
    g_frame_timing.max_push_us = max(g_frame_timing.max_push_us, push_us);
    g_frame_timing.max_interval_us = max(g_frame_timing.max_interval_us, g_frame_timing.interval_us);
}
