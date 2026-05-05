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

void InitGlitchPool();
void PrepareLayoutFromRule(const char* rule, SystemLang_t lang, uint16_t color, TextLayout& out);
void DrawChaosFrame(SystemLang_t lang, uint16_t color);
void DrawDoneFrame(const TextLayout& layout, int scrollOffset);
void PlayDecodeSequence(TextLayout& layout, int decodeStyle, ProcedureTick procedureTick);

int MaxVisibleLines(SystemLang_t lang);

} // namespace UIPrescript
