/*
【模块职责】菜单渲染器注册表和当前滚轮渲染器适配层。

第一阶段保留 AppMenuBase 原有绘制实现，先把“页面请求绘制”和“具体样式选择”
分开。这样新增第二套菜单时可以直接增加一个独立 UIMenuRenderer，而不必改动
旋钮、按键、陀螺仪事件和菜单条目业务代码。
*/
#include "ui/ui_menu_renderer.h"
#include "apps/app_menu_base.h"

namespace
{
UIWheelMenuRenderer g_wheel_menu_renderer;
}

UIMenuRenderer &UI_MenuRendererForStyle(UIMenuRenderStyle style)
{
    switch (style)
    {
    case UIMenuRenderStyle::Wheel:
    default:
        return g_wheel_menu_renderer;
    }
}

void UIWheelMenuRenderer::draw(AppMenuBase &menu, float visual_position)
{
    // 当前滚轮布局仍由 AppMenuBase 的兼容实现提供，确保本阶段画面和操作完全不变。
    // 后续渲染器可以直接读取同一菜单页面状态，实现全新的整屏布局。
    menu.drawWheelMenuUI(visual_position);
}

