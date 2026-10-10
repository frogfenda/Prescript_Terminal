/*
【模块职责】菜单整屏渲染器的替换接口。

菜单页面继续由 AppMenuBase 管理输入、选中索引、条目内容和业务跳转；
本接口只负责把当前菜单状态画到屏幕上。当前只注册原有滚轮渲染器，
后续新增列表、卡片或图标样式时，不需要修改菜单页面的输入逻辑。
*/
#pragma once

#include <Arduino.h>

class AppMenuBase;

/** 当前可用的菜单视觉样式。后续新增样式时扩展枚举和注册表即可。 */
enum class UIMenuRenderStyle : uint8_t
{
    Wheel = 0
};

/** 菜单渲染器公共接口；输入和菜单内容不经过这里重新解释。 */
class UIMenuRenderer
{
public:
    virtual ~UIMenuRenderer() {}

    /** 根据页面当前状态绘制一帧完整菜单。 */
    virtual void draw(AppMenuBase &menu, float visual_position) = 0;
};

/** 当前菜单的兼容实现，保持原有左 HUD + 右侧弧形滚轮效果。 */
class UIWheelMenuRenderer final : public UIMenuRenderer
{
public:
    void draw(AppMenuBase &menu, float visual_position) override;
};

/** 返回指定样式的渲染器；未注册样式统一回退到当前滚轮实现。 */
UIMenuRenderer &UI_MenuRendererForStyle(UIMenuRenderStyle style);

