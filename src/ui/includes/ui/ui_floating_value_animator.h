/*
【模块职责】提供“数值增量向上漂浮并逐渐淡出”的通用非阻塞动画。
【调用关系】硬币技能点数和业力累计次数在业务值变化时触发；页面主循环调用 update() 决定是否重绘，绘制阶段调用 draw()。
【重要约束】本类不清屏、不推屏、不修改业务数值；文本会复制到固定缓冲区，不保留调用方临时字符串。
*/
#pragma once

#include <Arduino.h>
#include <string.h>

#include "hal/hal.h"
#include "ui/ui_theme.h"

class UIFloatingValueAnimator
{
private:
    static constexpr uint16_t DURATION_MS = 640;
    static constexpr int RISE_PIXELS = 14;

    char text_[24] = {0};
    uint16_t color_ = TFT_WHITE;
    uint32_t started_ms_ = 0;
    uint32_t last_frame_ms_ = 0;
    bool active_ = false;

    float progress() const
    {
        if (!active_)
            return 1.0f;
        const uint32_t elapsed = millis() - started_ms_;
        if (elapsed >= DURATION_MS)
            return 1.0f;
        return (float)elapsed / (float)DURATION_MS;
    }

public:
    /**
     * 【接口说明】从头触发一次漂浮动画；连续触发时用新文本替换尚未结束的旧动画。
     * 【参数】text会立即复制；color是RGB565颜色。
     * 【线程约束】仅在Arduino主循环所属的页面事件中调用。
     */
    void trigger(const char *text, uint16_t color)
    {
        snprintf(text_, sizeof(text_), "%s", text ? text : "");
        color_ = color;
        started_ms_ = millis();
        last_frame_ms_ = started_ms_;
        active_ = text_[0] != '\0';
    }

    /** 清除动画；用于页面重新进入或业务状态重置。 */
    void reset()
    {
        text_[0] = '\0';
        active_ = false;
        started_ms_ = 0;
        last_frame_ms_ = 0;
    }

    /**
     * 【接口说明】按统一快速帧率推进动画。
     * 【返回值】true表示画面发生变化，调用方应重绘相关区域；结束帧也会返回一次true。
     */
    bool update()
    {
        if (!active_)
            return false;

        const uint32_t now = millis();
        if (now - last_frame_ms_ < UITheme::FRAME_FAST_MS)
            return false;

        last_frame_ms_ = now;
        if (now - started_ms_ >= DURATION_MS)
            active_ = false;
        return true;
    }

    bool isActive() const { return active_; }
    int width() const { return HAL_Get_Text_Width(text_); }

    /**
     * 【接口说明】以x/y为动画起点绘制当前文本；内部只改变纵向位置和淡出程度。
     * 【调用约束】调用方必须先画好背景和常驻数值，本函数不擦除上一帧。
     */
    void draw(int x, int y) const
    {
        if (!active_)
            return;

        const float p = progress();
        const int draw_y = y - (int)(p * RISE_PIXELS);
        HAL_Screen_ShowChineseLine_Faded_Color(x, draw_y, text_, p, color_);
    }
};
