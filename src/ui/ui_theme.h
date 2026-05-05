#pragma once
#include <Arduino.h>
#include "../sys/sys_constants.h"

// Shared visual constants for Prescript Terminal UI.
// Keep low-level screen geometry in PrescriptConst; keep style/layout values here.
namespace UITheme {

constexpr uint16_t COLOR_ACCENT = PrescriptConst::UI_ACCENT_SENTINEL;
constexpr uint16_t COLOR_DIM    = 0x8410;
constexpr uint16_t COLOR_DARK   = 0x2104;
constexpr uint16_t COLOR_WARN   = 0xFBE0;
constexpr uint16_t COLOR_CYAN   = 0x07FF;

constexpr uint16_t FRAME_FAST_MS   = 16;  // ~60 FPS for short high-impact animations.
constexpr uint16_t FRAME_NORMAL_MS = 33;  // ~30 FPS for steady UI / power-friendly animation.
constexpr uint16_t FRAME_SLOW_MS   = 100;

constexpr int SAFE_MARGIN_X = 16;

namespace Menu {
constexpr int PaddingX       = 10;
constexpr int LineMarginY    = 8;
constexpr int ItemSpacingY   = 24;
constexpr int ScanBoxPadX    = 6;
constexpr int ScanBoxHeight  = 24;
constexpr int ScrollBarWidth = 5;
constexpr float CurveFactor  = 12.0f;
}

namespace EditFlow {
constexpr int LinkY       = 2;
constexpr int DividerY    = 18;
constexpr int DialY       = 28;
constexpr int TipY        = 56;
constexpr float TipFade   = 0.6f;
}

namespace Dialog {
constexpr int TitleX      = 10;
constexpr int TextY       = 26;
constexpr int RightPadX   = 10;
constexpr int TipY        = 56;
constexpr float TipFade   = 0.6f;
}

namespace Volume {
constexpr int FrameMs       = FRAME_NORMAL_MS;
constexpr int DividerX      = PrescriptConst::UI_SCREEN_WIDTH / 2;
constexpr int DividerTopY   = 15;
constexpr int DividerBottomY= 63;
constexpr int SliderY       = 42;
constexpr int SliderW       = 100;
constexpr int SliderH       = 10;
constexpr int LeftX         = 10;
constexpr int RightX        = 155;
constexpr float SnapEpsilon = 0.15f;
}

}
