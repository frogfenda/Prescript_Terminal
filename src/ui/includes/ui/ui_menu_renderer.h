/*
【模块职责】菜单整屏渲染器的替换接口。

菜单页面继续由 AppMenuBase 管理输入、选中索引、条目内容和业务跳转；
本接口只负责把当前菜单状态画到屏幕上。当前注册滚轮和纸带渲染器，
后续新增列表、卡片或图标样式时，不需要修改菜单页面的输入逻辑。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_hud.h"
#include "ui/ui_menu_ropes.h"

/** 菜单渲染器读取的稳定文本布局；字符串只在当前渲染帧内借用。 */
struct UIMenuTextLayout
{
    String lines[2];
    int line_widths[2] = {0, 0};
    uint8_t line_count = 0;
    int width = 0;
    int height = 0;
};

/**
 * 菜单页面给 UI 渲染器提供的最小视图接口。
 *
 * 页面仍拥有输入、选中状态和文本缓存；渲染器只通过本接口读取内容、更新视觉布局，
 * 不依赖 AppMenuBase 的具体类型或业务继承树。
 */
class UIMenuRenderSource
{
public:
    virtual ~UIMenuRenderSource() {}
    virtual int getMenuRenderCount() = 0;
    virtual const char *getMenuRenderTitle() = 0;
    virtual bool getMenuRenderLayout(int index, int max_width, UIMenuTextLayout &out) = 0;
    virtual bool getMenuRenderItemEditParts(int index, const char **prefix,
                                            const char **anim_val, const char **suffix) = 0;
    virtual uint16_t getMenuRenderItemColor(int index) = 0;
    virtual int getMenuRenderSelection() const = 0;
    virtual int getMenuRenderTargetPosition() const = 0;
    virtual float getMenuRenderVelocity() const = 0;
    virtual float getMenuRenderSlingDisplay() const = 0;
    virtual float getMenuRenderReflection() const = 0;
    virtual float getMenuRenderFrameDt() const = 0;
    virtual float getMenuRenderExpansion() const = 0;
    virtual float getMenuRenderBoxHeight() const = 0;
    virtual float getMenuRenderLabelWidth() const = 0;
    virtual bool getMenuRenderSettled() const = 0;
    virtual uint32_t getMenuRenderSettledSince() const = 0;
    virtual void setMenuRenderSettled(bool settled) = 0;
    virtual void setMenuRenderSettledSince(uint32_t now) = 0;
    virtual void setMenuRenderLayout(float expansion, float box_height,
                                     float label_width, bool animating) = 0;
    virtual void ensureMenuRenderState(int real_count) = 0;
    virtual void resetMenuRenderFrameDt() = 0;
    virtual UIMenuRopes &menuRenderRopes() = 0;
    virtual void drawMenuRenderLabel(int index, int center_x, int center_y,
                                     int max_width, uint16_t color) = 0;
    virtual void drawMenuRenderEditText(int x, int y, const char *prefix,
                                        const char *anim_val, const char *suffix,
                                        float distance = 0.0f) = 0;
};

/** 当前可用的菜单视觉样式。后续新增样式时扩展枚举和注册表即可。 */
enum class UIMenuRenderStyle : uint8_t
{
    Wheel = 0,
    PaperTape = 1
};

/** 菜单渲染器公共接口；输入和菜单内容不经过这里重新解释。 */
class UIMenuRenderer
{
public:
    virtual ~UIMenuRenderer() {}

    /** 根据页面当前状态绘制一帧完整菜单。 */
    virtual void draw(UIMenuRenderSource &menu, float visual_position,
                      const SysHudStatus &status) = 0;

    // 【生命周期】返回菜单或外部重新定位时清除视觉惯性；全部由主循环调用。
    virtual void reset(float position) { (void)position; }
    // 【动画接口】追踪未取模的目标槽位，返回是否进入新帧；默认样式仍由旧控制器推进。
    virtual bool update(float target, uint32_t now) { (void)target; (void)now; return false; }
    virtual float position() const { return 0.0f; }
};

/** 当前菜单的兼容实现，保持原有左 HUD + 右侧弧形滚轮效果。 */
class UIWheelMenuRenderer final : public UIMenuRenderer
{
public:
    void draw(UIMenuRenderSource &menu, float visual_position,
              const SysHudStatus &status) override;
};

/** 返回指定样式的渲染器；未注册样式统一回退到当前滚轮实现。 */
UIMenuRenderer &UI_MenuRendererForStyle(UIMenuRenderStyle style);

// 【样式入口】默认滚轮；只改变显示，保留页面内容和输入。运行时切换不写配置。
UIMenuRenderStyle UI_GetMenuRenderStyle();
void UI_SetMenuRenderStyle(UIMenuRenderStyle style);

/**
 * 滚轮实际帧耗时快照；主循环采集/读取，不打印串口，不计入页面内容。
 * draw_us 包含文字排版/绘制，push_us 包含旋转、TE 等待及 DMA 传输。
 * interval_us 是连续滚动帧的起点间隔，静止超过 250ms 后重新开始统计间隔。
 * 没有实机帧数据时 frames 为零，不用目标帧率冒充实际帧率。
 */
struct UIMenuFrameTiming
{
    uint32_t frames = 0;
    uint32_t draw_us = 0;
    uint32_t push_us = 0;
    uint32_t interval_us = 0;
    uint32_t max_draw_us = 0;
    uint32_t max_push_us = 0;
    uint32_t max_interval_us = 0;
};
UIMenuFrameTiming UI_GetMenuFrameTiming();
void UI_RecordMenuFrameTiming(uint32_t frame_start_us, uint32_t draw_us, uint32_t push_us);
