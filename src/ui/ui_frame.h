#pragma once
#include <Arduino.h>
#include "../hal/hal.h"
#include "ui_theme.h"
#include "ui_text.h"

namespace UIFrame {

void DrawTacticalDivider(int y = UITheme::EditFlow::DividerY, uint16_t color = UITheme::COLOR_ACCENT);
void DrawTip(const char* text, int y = UITheme::EditFlow::TipY, float fade = UITheme::EditFlow::TipFade);
void DrawDangerConfirm(const char* title, const char* message, const char* tip);
void DrawCornerBox(int x1, int x2, int center_y, int half_w, int h = 16, uint16_t color = UITheme::COLOR_ACCENT);

}
