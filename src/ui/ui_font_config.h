#pragma once

/*
【模块职责】终端字体度量配置。

字体文件本体放在同目录的 terminal_font.h；本文件只负责告诉 HAL：
- 当前字体的 baseline 是多少；
- 一行文本应占多少高度；
- 指令解码排版时，一个“字符单元”大致按多少像素估算。

注意：
U8g2 点阵字体不能在 ESP32 上任意缩放。字号由开发端转换字体时决定。
换字体或字号后，如果 UI 上下不居中，优先调 baseline；如果行距拥挤，调 lineHeight；
如果指令文本换行过早或溢出，调 cellWidth。
*/

#include <Arduino.h>
#include "terminal_font.h"

#ifndef TERMINAL_FONT_BASELINE
#define TERMINAL_FONT_BASELINE 16
#endif

#ifndef TERMINAL_FONT_LINE_HEIGHT
#define TERMINAL_FONT_LINE_HEIGHT 20
#endif

#ifndef TERMINAL_FONT_CELL_WIDTH
#define TERMINAL_FONT_CELL_WIDTH 16
#endif

namespace UIFontConfig {

enum class Role : uint8_t {
    Small,
    Body,
    Title
};

struct FontSpec {
    const uint8_t* font;
    uint8_t baseline;
    uint8_t lineHeight;
    uint8_t cellWidth;
    const char* name;
};

/*
 * 当前全机只使用一套字体。
 * Small / Body / Title 保留为语义角色，是为了不让 App 直接依赖字体名；
 * 三个角色返回相同字体和度量，后续仍然符合“全篇只用一个字体”的要求。
 */
static inline FontSpec Unified(const char* roleName)
{
    return {
        TERMINAL_FONT,
        TERMINAL_FONT_BASELINE,
        TERMINAL_FONT_LINE_HEIGHT,
        TERMINAL_FONT_CELL_WIDTH,
        roleName
    };
}

static inline FontSpec Small() { return Unified("SINGLE_SMALL"); }
static inline FontSpec Body()  { return Unified("SINGLE_BODY"); }
static inline FontSpec Title() { return Unified("SINGLE_TITLE"); }

static inline FontSpec Get(Role role)
{
    switch (role)
    {
    case Role::Small: return Small();
    case Role::Title: return Title();
    case Role::Body:
    default: return Body();
    }
}

static inline uint8_t CellWidth()
{
    return TERMINAL_FONT_CELL_WIDTH;
}

} // namespace UIFontConfig
