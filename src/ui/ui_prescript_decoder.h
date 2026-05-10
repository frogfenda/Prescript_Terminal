/*
【模块职责】指令解码渲染接口。AppPrescript 把指令文本、语言、颜色和动画模式交给这里，渲染乱码态、解码态和完成态。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include "../sys/app_manager.h"

namespace UIPrescript {

using ProcedureTick = void (*)();

struct TextLayout
{
    static const int MaxLines = 20;
    char lines[MaxLines][256];
    int actualLines = 0;
    SystemLang_t lang = LANG_ZH;
    uint16_t color = 1;
};

// 【接口说明】重建解码乱码池，混合中文伪字符和终端符号，为 CHAOS 和四种解码动画提供随机字符来源。
void InitGlitchPool();
void PrepareLayoutFromRule(const char* rule, SystemLang_t lang, uint16_t color, TextLayout& out);
// 【接口说明】绘制尚未解码的乱码矩阵帧，按语言选择字符密度并使用传入颜色作为故障主题色。
void DrawChaosFrame(SystemLang_t lang, uint16_t color);
void DrawDoneFrame(const TextLayout& layout, int scrollOffset);
// 【接口说明】按 decodeStyle 播放四种阻塞式解码动画，并通过 procedureTick 维持 procedure.wav 循环音效。
void PlayDecodeSequence(TextLayout& layout, int decodeStyle, ProcedureTick procedureTick);

int MaxVisibleLines(SystemLang_t lang);

} // namespace UIPrescript
