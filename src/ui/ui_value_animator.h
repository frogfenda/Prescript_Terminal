/*
【模块职责】菜单数值编辑动画引擎。

这个文件把原来写在 AppBase 里的“三段式数值跳动动画”抽到 UI 层：
- AppPushSetting 的“指令推送: 开启 / 最短潜伏: 15 分钟”；
- AppCoinSettings 的数值编辑；
- AppTimeSetting 的“周期校时: 开启 / 校时间隔: 15 分钟”；
都会使用同一套 UIValueAnimator。

动画规则：
- 菜单项文本被拆成 prefix + value + suffix 三段；
- prefix 和 suffix 稳定显示；
- value 在旋钮变化时按方向上下跳动，并带淡出残影；
- update() 用 UITheme::FRAME_FAST_MS 锁帧，避免高频重绘导致屏幕闪烁。
*/
#pragma once

#include <Arduino.h>
#include "../hal/hal.h"
#include "ui_theme.h"

class UIValueAnimator
{
private:
    float progress = 0.0f;        // 当前动画强度：1.0 表示刚触发，逐帧衰减到 0。
    int direction = 0;            // 旋钮方向：1 表示向上跳，-1 表示向下跳。
    uint32_t last_tick_ms = 0;    // 上一帧动画更新时间，用于锁定刷新率。

public:
    /**
     * 触发一次数值变化动画。
     *
     * delta 来自旋钮步进：
     * - delta > 0：动态值向一个方向跳动；
     * - delta < 0：动态值向相反方向跳动。
     *
     * 这个函数只记录动画起点，不直接推屏；
     * 页面会在 onLoop() 中调用 update() 判断是否需要重绘。
     */
    void trigger(int delta)
    {
        direction = delta > 0 ? 1 : -1;
        progress = 1.0f;
        last_tick_ms = millis();
    }

    /**
     * 推进一帧编辑动画。
     *
     * 返回 true：
     *   动画进入了下一帧，调用者应该重绘菜单；
     *
     * 返回 false：
     *   动画没有变化，或者还没到下一帧时间，调用者不需要推屏。
     */
    bool update()
    {
        if (progress <= 0.0f)
            return false;

        uint32_t now = millis();
        if (now - last_tick_ms < UITheme::FRAME_FAST_MS)
            return false;

        progress -= 0.15f;
        last_tick_ms = now;

        if (progress <= 0.0f)
        {
            progress = 0.0f;
            direction = 0;
        }

        return true;
    }

    /**
     * 绘制三段式菜单文本。
     *
     * 用途：
     * - 菜单项在编辑状态下显示“前缀 + 动态值 + 后缀”；
     * - 只有动态值 value 会跟随 progress 上下跳动；
     * - 当前菜单项离中心越远，distance 越大，整体绘制越淡。
     *
     * 参数：
     * - x/y：整段文本左上角坐标；
     * - prefix：固定前缀，例如“周期校时: ”；
     * - value：正在编辑的动态值，例如“开启”或“15”；
     * - suffix：固定后缀，例如“ 分钟 <”；
     * - distance：AppMenuBase 的滚轮淡出距离。
     */
    void drawSegmentedText(int x, int y,
                           const char* prefix,
                           const char* value,
                           const char* suffix,
                           float distance = 0.0f)
    {
        int current_x = x;

        // 1. 前缀稳定显示，用于说明当前编辑项含义。
        if (prefix && prefix[0] != '\0')
        {
            if (distance > 0.01f)
                HAL_Screen_ShowChineseLine_Faded(current_x, y, prefix, distance);
            else
                HAL_Screen_ShowChineseLine(current_x, y, prefix);

            current_x += HAL_Get_Text_Width(prefix);
        }

        // 2. 动态值带跳动效果，是用户旋钮操作时的视觉反馈核心。
        if (value && value[0] != '\0')
        {
            int value_y = y;
            float value_distance = distance;

            if (progress > 0.01f)
            {
                value_y += (int)(progress * direction * 12.0f);
                value_distance += progress * 1.5f;
            }

            if (value_distance > 0.01f)
                HAL_Screen_ShowChineseLine_Faded(current_x, value_y, value, value_distance);
            else
                HAL_Screen_ShowChineseLine(current_x, value_y, value);

            current_x += HAL_Get_Text_Width(value);
        }

        // 3. 后缀稳定显示，通常用于单位、编辑标记或提示符。
        if (suffix && suffix[0] != '\0')
        {
            if (distance > 0.01f)
                HAL_Screen_ShowChineseLine_Faded(current_x, y, suffix, distance);
            else
                HAL_Screen_ShowChineseLine(current_x, y, suffix);
        }
    }
};
