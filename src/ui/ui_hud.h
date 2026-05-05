#pragma once
#include <Arduino.h>

// Draws the left HUD panel used by AppMenuBase and returns the panel width.
int UIHud_DrawLeftPanel(const char* title_text);

// True when HUD content changed by second/minute ticks, NFC emulation, TT2 timer, etc.
bool UIHud_NeedsRedraw();
