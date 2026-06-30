#pragma once
#include <Arduino.h>
#include "hal/hal.h"

namespace UIText {

/**
 * 返回 UTF-8 字符首字节对应的字节长度。
 *
 * 这个工具被指令解码动画、列表裁剪绘制和文本换行共同使用。
 * 它只判断首字节模式，不做完整 Unicode 合法性校验；项目内文本来自
 * LittleFS/云端/网页协议，遇到异常字节时按 1 字节跳过，避免死循环。
 */
int Utf8CharLen(unsigned char c);

/**
 * 估算一个 UTF-8 字符的显示宽度。
 *
 * text 指向当前字符首字节，len 是该字符的 UTF-8 字节数。
 * 函数内部会临时截出一个字符交给 HAL 当前字体测量；如果测量失败，
 * 空格按较窄宽度处理，避免滚动/换行时空格占用过大。
 */
int CharWidth(const char* text, int len);

/**
 * 使用 HAL 当前正文/全局字体测量整段文本宽度。
 * 菜单居中、弹窗标题居中和滚动条布局都会依赖这个结果。
 */
int Measure(const char* text);

/** 使用当前全局字体绘制一行普通文本。 */
void Draw(int x, int y, const char* text);

/** 使用当前全局字体绘制一行淡出文本，常用于底部提示和低优先级状态。 */
void DrawFaded(int x, int y, const char* text, float fade);

/** 按屏幕宽度居中绘制一行普通文本。 */
void DrawCentered(int y, const char* text);

/** 按屏幕宽度居中绘制一行淡出文本。 */
void DrawCenteredFaded(int y, const char* text, float fade = 0.6f);

/**
 * 裁剪绘制一行文本。
 *
 * 函数会逐个 UTF-8 字符测量宽度，只绘制落在 [min_x, max_x] 范围内的字形。
 * 主要用于指令档案、滚轮菜单等不希望文字越界覆盖边框的场景。
 */
void DrawClipped(int x, int y, const char* text, int min_x = 0, int max_x = -1);

/** 裁剪绘制淡出文本，用于弱化状态或渐隐列表项。 */
void DrawClippedFaded(int x, int y, const char* text, float fade, int min_x = 0, int max_x = -1);

/**
 * 双语文本选择小工具。
 *
 * 只做简单三元选择，不访问 appManager，避免 UI 工具层反向依赖 App 层。
 */
inline const char* Pick(bool zh, const char* zh_text, const char* en_text) {
    return zh ? zh_text : en_text;
}

} // namespace UIText
