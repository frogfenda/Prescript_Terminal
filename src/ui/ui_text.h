#pragma once
#include <Arduino.h>
#include "../hal/hal.h"

namespace UIText {

int Utf8CharLen(unsigned char c);
int CharWidth(const char* text, int len);
int Measure(const char* text);

void Draw(int x, int y, const char* text);
void DrawFaded(int x, int y, const char* text, float fade);
void DrawCentered(int y, const char* text);
void DrawCenteredFaded(int y, const char* text, float fade);
void DrawClipped(int x, int y, const char* text, int min_x = 0, int max_x = -1);
void DrawClippedFaded(int x, int y, const char* text, float fade, int min_x = 0, int max_x = -1);

// Small wrapper for future bilingual builds. For now it simply returns zh/en by flag,
// but it keeps call sites from embedding language branching deeper into UI helpers.
inline const char* Pick(bool zh, const char* zh_text, const char* en_text) {
    return zh ? zh_text : en_text;
}

}
