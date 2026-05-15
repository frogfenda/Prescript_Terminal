/*
【模块职责】响应式 UI 主题接口。

本文件集中管理菜单、链路编辑页、弹窗、音量滑条、滚轮动画和字体行高等通用尺寸。
当前新屏分支的逻辑画布为 428×142，因此所有位置/尺寸都按当前屏幕宽高计算，
避免旧 284×76 屏幕时代的固定坐标继续散落在 App 中。

接口约定：
- 本文件统一使用“函数式接口”，例如 UITheme::Menu::PaddingX()；
- 调用方不要再使用 UITheme::Menu::PaddingX 这种常量式写法；
- 这样后续换屏或调整字号时，可以在这里根据屏幕和字体参数动态计算布局。
*/
#pragma once
#include <Arduino.h>
#include "../sys/sys_constants.h"
#include "ui_font_config.h"

namespace UITheme {

// -----------------------------------------------------------------------------
// 颜色与动画节奏
// -----------------------------------------------------------------------------
constexpr uint16_t COLOR_ACCENT = PrescriptConst::UI_ACCENT_SENTINEL;
constexpr uint16_t COLOR_DIM    = 0x8410;
constexpr uint16_t COLOR_DARK   = 0x2104;
constexpr uint16_t COLOR_WARN   = 0xFBE0;
constexpr uint16_t COLOR_CYAN   = 0x07FF;

constexpr uint16_t FRAME_FAST_MS   = 16;  // 约 60FPS：用于菜单惯性、滚轮回弹、短促动画。
constexpr uint16_t FRAME_NORMAL_MS = 33;  // 约 30FPS：用于稳定状态动画，降低屏幕和 CPU 压力。
constexpr uint16_t FRAME_SLOW_MS   = 100;

inline int ScreenW() { return PrescriptConst::UI_SCREEN_WIDTH; }
inline int ScreenH() { return PrescriptConst::UI_SCREEN_HEIGHT; }

// UI 边缘安全区。字体变大后，弧形滚轮文字更容易贴边，所以安全区随屏幕宽度略放大。
inline int SafeMarginX()
{
    return max(18, ScreenW() / 22);
}

// -----------------------------------------------------------------------------
// 字体布局参数
// -----------------------------------------------------------------------------
namespace Font {

inline int SmallBaseline()   { return UIFontConfig::Small().baseline; }
inline int SmallLineHeight() { return UIFontConfig::Small().lineHeight; }
inline int BodyBaseline()    { return UIFontConfig::Body().baseline; }
inline int BodyLineHeight()  { return UIFontConfig::Body().lineHeight; }
inline int TitleBaseline()   { return UIFontConfig::Title().baseline; }
inline int TitleLineHeight() { return UIFontConfig::Title().lineHeight; }

}

// -----------------------------------------------------------------------------
// 主菜单滚轮布局
// -----------------------------------------------------------------------------
namespace Menu {

inline int PaddingX()       { return max(14, ScreenW() / 30); }
inline int LineMarginY()    { return max(12, ScreenH() / 13); }
inline int ItemSpacingY()   { return max(Font::BodyLineHeight() + 6, ScreenH() / 5); }
// 中心项静止时回到 HUD 竖线右侧区域的视觉中心；
// 只有旋钮正在滚动时，才按速度向左产生弹性甩动。
// CenterMinLeftGap 是动态甩动的左侧保护距离，避免长文本或高速旋转时贴到 HUD 竖线。
inline int CenterMinLeftGap()   { return (ScreenW() >= 400) ? 18 : 10; }
inline int CenterFlingX()       { return (ScreenW() >= 400) ? 24 : 14; }
inline int ScanBoxPadX()    { return max(8,  ScreenW() / 48); }
inline int ScanBoxHeight()  { return max(Font::BodyLineHeight() + 8, ScreenH() / 5); }
inline int ScrollBarWidth() { return max(6,  ScreenW() / 72); }
inline float CurveFactor()  { return (ScreenW() >= 400) ? 44.0f : 28.0f; }
inline float CurveSpeedBoost() { return (ScreenW() >= 400) ? 28.0f : 18.0f; }
inline float SlingSpeedBoost() { return (ScreenW() >= 400) ? 8.0f : 5.0f; }

}

// -----------------------------------------------------------------------------
// 链路 + 滚轮编辑页布局
// -----------------------------------------------------------------------------
namespace EditFlow {

// 顶部链路、分隔线、滚轮和底部提示形成四段布局。
// 这些位置和字体行高有关，字体放大后不再沿用旧屏 2/18/28/56 的固定值。
inline int LinkY()    { return max(8,  ScreenH() / 10); }
inline int DividerY() { return max(38, ScreenH() / 3); }
inline int DialY()    { return max(62, (ScreenH() * 47) / 100); }
inline int TipY()     { return ScreenH() - max(Font::SmallLineHeight() + 8, ScreenH() / 7); }
inline float TipFade(){ return 0.6f; }

}

// -----------------------------------------------------------------------------
// 弹窗 / 确认页布局
// -----------------------------------------------------------------------------
namespace Dialog {

inline int TitleX()    { return max(16, ScreenW() / 26); }
inline int TextY()     { return max(40, ScreenH() / 3); }
inline int RightPadX() { return max(16, ScreenW() / 30); }
inline int TipY()      { return EditFlow::TipY(); }
inline float TipFade() { return 0.6f; }

}

// -----------------------------------------------------------------------------
// 音量 / 震动双滑条布局
// -----------------------------------------------------------------------------
namespace Volume {

constexpr float SnapEpsilon = 0.15f;
inline int FrameMs()        { return FRAME_NORMAL_MS; }
inline int DividerX()       { return ScreenW() / 2; }
inline int DividerTopY()    { return max(22, ScreenH() / 6); }
inline int DividerBottomY() { return ScreenH() - max(22, ScreenH() / 6); }
inline int SliderY()        { return (ScreenH() * 58) / 100; }
inline int SliderH()        { return max(12, ScreenH() / 10); }

// 每侧滑条宽度根据半屏宽度计算，同时预留百分比文字和外边距。
inline int SliderW()
{
    int half = ScreenW() / 2;
    return constrain(half - 76, 112, 160);
}

inline int LeftX()  { return max(16, ScreenW() / 18); }
inline int RightX() { return DividerX() + max(22, ScreenW() / 22); }

}

// -----------------------------------------------------------------------------
// 滚轮动画尺寸
// -----------------------------------------------------------------------------
namespace Dial {

inline float NumberRadius() { return min(190.0f, ScreenW() * 0.42f); }
inline float StringRadius() { return min(205.0f, ScreenW() * 0.43f); }
inline int NumberBoxHalfW() { return max(28, ScreenW() / 14); }
inline int StringBoxHalfW() { return max(54, ScreenW() / 7); }
inline int BoxHeight()      { return max(Font::BodyLineHeight() + 6, ScreenH() / 7); }
inline int SuffixGap()      { return max(38, ScreenW() / 12); }

}

// -----------------------------------------------------------------------------
// 顶部流程链路动画尺寸
// -----------------------------------------------------------------------------
namespace Link {

inline int BoxPadX() { return max(5, ScreenW() / 90); }
inline int Corner()  { return max(3, ScreenW() / 140); }

// 各 App 仍可以传入自己偏好的 spacing；这里根据新屏宽度给过小的值抬底。
inline int Spacing(int count, int requested)
{
    if (ScreenW() < 400)
        return requested;

    int min_spacing = (count <= 2) ? 132 : 94;
    return max(requested, min_spacing);
}

}

// -----------------------------------------------------------------------------
// 通用机械框线尺寸
// -----------------------------------------------------------------------------
namespace Frame {

inline int DividerCenterHalfGap() { return max(38, ScreenW() / 10); }
inline int DividerBevelW()        { return max(6,  ScreenW() / 70); }
inline int DividerBevelH()        { return max(4,  ScreenH() / 36); }
inline int CornerSize()           { return max(5,  ScreenW() / 90); }

}

} // namespace UITheme
