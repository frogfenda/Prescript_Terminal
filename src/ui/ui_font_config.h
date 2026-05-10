/*
【模块职责】终端全局单字体配置。

当前设计目标：
- 开发端选定一套字体和字号；
- 固件编译后整机固定使用这套字体；
- 用户不能在设备端修改字体或字号；
- UI / App 代码不直接依赖具体字体名，只通过 HAL 字体接口绘制。

为什么仍保留 Small / Body / Title 三个角色：
- 现有 HAL 和 UI 代码已经用角色描述“这个文字用于 HUD / 正文 / 标题”；
- 但在当前单字体策略下，这三个角色全部指向同一个 TERMINAL_FONT；
- 后续如果你仍然坚持全篇单字体，只需要继续让三个角色返回同一字体。

换字体时通常只改 4 处：
1. src/fonts/terminal_font.h：放入转换后的 U8g2 字体数组；
2. TERMINAL_FONT_BASELINE：调文字基线；
3. TERMINAL_FONT_LINE_HEIGHT：调行高和菜单/滚轮间距；
4. TERMINAL_FONT_CELL_WIDTH：调指令换行时的字符宽度估算。
*/
#pragma once

#include <Arduino.h>
#include <U8g2_for_TFT_eSPI.h>
#include "../fonts/terminal_font.h"

// -----------------------------------------------------------------------------
// 开发端单字体选择
// -----------------------------------------------------------------------------
// 全局唯一字体。默认来自 src/fonts/terminal_font.h。
#ifndef TERMINAL_FONT
#define TERMINAL_FONT terminal_custom_font
#endif

// 字体基线：HAL 绘制时会把 y 视为文字框顶部，再加 baseline 写入 U8g2 cursor。
// 如果换字体后文字偏上、偏下或被裁切，优先微调这个值。
#ifndef TERMINAL_FONT_BASELINE
#define TERMINAL_FONT_BASELINE 16
#endif

// 行高：菜单、滚轮、链路编辑页、指令正文等会用它计算纵向排布。
// 字号变大后，需要同步增大行高，否则上下行会挤在一起。
#ifndef TERMINAL_FONT_LINE_HEIGHT
#define TERMINAL_FONT_LINE_HEIGHT 22
#endif

// 字符单元宽度估算：用于指令解码和文本排版的粗略列数计算。
// 中文等宽字体通常接近字号；如果文本换行过早，调小；如果右侧溢出，调大。
#ifndef TERMINAL_FONT_CELL_WIDTH
#define TERMINAL_FONT_CELL_WIDTH 16
#endif

namespace UIFontConfig {

/** 字体角色仍保留为语义标签，但当前全部映射到同一套 TERMINAL_FONT。 */
enum class Role : uint8_t {
    Small,
    Body,
    Title
};

/** 单个字体角色的完整参数。当前单字体策略下，三种角色返回相同参数。 */
struct FontSpec {
    const uint8_t* font;
    uint8_t baseline;
    uint8_t lineHeight;
    uint8_t cellWidth;
    const char* name;
};

/** 返回全局唯一字体配置。 */
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

/** 小字角色：当前仍使用全局唯一字体，只保留调用语义。 */
static inline FontSpec Small()
{
    return Unified("SINGLE_FONT_SMALL");
}

/** 正文角色：当前仍使用全局唯一字体，只保留调用语义。 */
static inline FontSpec Body()
{
    return Unified("SINGLE_FONT_BODY");
}

/** 标题角色：当前仍使用全局唯一字体，只保留调用语义。 */
static inline FontSpec Title()
{
    return Unified("SINGLE_FONT_TITLE");
}

/** 根据角色返回字体参数。当前三种角色最终都返回同一套字体。 */
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

/** 返回当前开发端设置的字符单元宽度估算。 */
static inline uint8_t CellWidth()
{
    return TERMINAL_FONT_CELL_WIDTH;
}

} // namespace UIFontConfig
