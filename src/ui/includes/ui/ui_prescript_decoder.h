#pragma once
#include <Arduino.h>
#include "lang/terminal_lang.h"

/*
【模块职责】指令解码渲染接口。

AppPrescript 只把指令文本、语言、主题颜色和动画模式交给本模块；
本模块负责把文本排版到当前逻辑屏幕，并绘制以下三类画面：
- CHAOS：尚未确认前的乱码矩阵；
- DECODE：四种可设置的解码动画；
- DONE：解码完成后的可滚动文本。

当前大屏分支不再使用旧 284×76 固定行列，行数、列数、滚动条位置都会根据 HAL 屏幕尺寸和字体参数计算。
*/
namespace UIPrescript {

using ProcedureTick = void (*)();

struct TextLayout
{
    static const int MaxLines = 20;

    // 每行最终排版后的 UTF-8 文本。动画阶段不重新拆行，保证完成态和解码态位置一致。
    char lines[MaxLines][256];

    // 每行最终文本的像素宽度与居中后的 x 坐标。
    // 这些值在 PrepareLayoutFromRule() 后一次性计算，避免动画每帧因为乱码宽度不同而左右抖动。
    int lineW[MaxLines] = {0};
    int lineX[MaxLines] = {0};

    // 短指令完成态/解码态使用垂直居中；长指令保持顶部起始并允许滚动。
    int contentStartY = 0;

    int actualLines = 0;
    SystemLang_t lang = LANG_ZH;
    uint16_t color = 1;
};

/** 初始化中文乱码池，为 CHAOS 和解码动画生成“系统故障”字符。 */
void InitGlitchPool();

/** 把原始指令文本按当前屏幕宽度和字体配置排成多行。 */
void PrepareLayoutFromRule(const char* rule, SystemLang_t lang, uint16_t color, TextLayout& out);

/** 绘制确认前的乱码矩阵帧。AppPrescript 在 S_CHAOS 状态下每帧调用。 */
void DrawChaosFrame(SystemLang_t lang, uint16_t color);

/** 绘制完成态文本和右侧滚动条。scrollOffset 表示当前从第几行开始显示。 */
void DrawDoneFrame(const TextLayout& layout, int scrollOffset);

/** 播放四种阻塞式解码动画；procedureTick 用来维持 procedure.wav 循环声。 */
void PlayDecodeSequence(TextLayout& layout, int decodeStyle, ProcedureTick procedureTick);

/** 返回当前字体和屏幕下最多能显示多少行完成态文本。 */
int MaxVisibleLines(SystemLang_t lang);

} // namespace UIPrescript
