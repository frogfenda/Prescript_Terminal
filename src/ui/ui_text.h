/*
【模块职责】UI 文本工具接口。封装 UTF-8 字符长度、文本宽度、居中绘制、裁剪绘制和淡出绘制。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include "../hal/hal.h"

namespace UIText {

// 【接口说明】根据 UTF-8 首字节判断当前字符占用 1/2/3/4 字节，供逐字测量和裁剪使用。
int Utf8CharLen(unsigned char c);
int CharWidth(const char* text, int len);
// 【接口说明】逐 UTF-8 字符累加宽度，得到与 U8g2 绘制一致的字符串宽度。
int Measure(const char* text);

void Draw(int x, int y, const char* text);
// 【接口说明】在指定坐标绘制带 distance 灰度衰减的文本。
void DrawFaded(int x, int y, const char* text, float fade);
void DrawCentered(int y, const char* text);
// 【接口说明】水平居中绘制带灰度衰减的文本。
void DrawCenteredFaded(int y, const char* text, float fade);
void DrawClipped(int x, int y, const char* text, int min_x = 0, int max_x = -1);
// 【接口说明】逐字符裁剪绘制淡出文本，刻度盘和流程链路用它避免文字越界。
void DrawClippedFaded(int x, int y, const char* text, float fade, int min_x = 0, int max_x = -1);

// Small wrapper for future bilingual builds. For now it simply returns zh/en by flag,
// but it keeps call sites from embedding language branching deeper into UI helpers.
// 【函数说明】根据布尔语言标志返回中文或英文字符串，减少调用点直接写三元表达式。
inline const char* Pick(bool zh, const char* zh_text, const char* en_text) {
    return zh ? zh_text : en_text;
}

}
