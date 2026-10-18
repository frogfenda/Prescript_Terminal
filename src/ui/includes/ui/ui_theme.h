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
#include "sys/sys_constants.h"
#include "ui/ui_font_config.h"

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

// 阻尼响应固定，不随瞬时输入速度变更，防止减速阶段出现节奏突变。
constexpr float MotionResponse = 24.0f;
constexpr float LayoutResponse = 22.0f;
constexpr uint32_t ExpandDelayMs = 80;
constexpr float ReflectionResponse = 10.0f;
constexpr float ReflectionVelocity = 12.0f; // 槽位/秒，决定反光随滚动速度变化的强度。
constexpr float ArcSpread = 1.08f; // 以中心槽为锚点，将上下圆弧展开约 8%。

inline int PaddingX()       { return max(14, ScreenW() / 30); }
inline int LineMarginY()    { return max(12, ScreenH() / 13); }
inline int ItemSpacingY()   { return max(Font::BodyLineHeight() + 6, ScreenH() / 5); }
// 中心项静止时回到 HUD 竖线右侧区域的视觉中心略偏左；
// 只有旋钮正在滚动时，才按速度向左产生弹性甩动。
// CenterMinLeftGap 是动态甩动的左侧保护距离，避免长文本或高速旋转时贴到 HUD 竖线。
inline int CenterMinLeftGap()   { return (ScreenW() >= 400) ? 18 : 10; }
inline int CenterRestOffsetX()  { return (ScreenW() >= 400) ? 8 : 5; }
inline int CenterFlingX()       { return (ScreenW() >= 400) ? 24 : 14; }
inline int ScanBoxPadX()    { return max(8,  ScreenW() / 48); }
inline int ScanBoxHeight()  { return max(Font::BodyLineHeight() + 8, ScreenH() / 5); }
inline int ScrollBarWidth() { return max(6,  ScreenW() / 72); }
inline float CurveFactor()  { return (ScreenW() >= 400) ? 48.0f : 30.0f; }
inline float CurveSpeedBoost() { return (ScreenW() >= 400) ? 28.0f : 18.0f; }
inline float SlingSpeedBoost() { return (ScreenW() >= 400) ? 8.0f : 5.0f; }

}

// 纸带布局以屏幕和原字体为依据；上下给状态栏留空间，中间保留两行正文。
namespace PaperMenu {
constexpr uint16_t Paper = 0x18C3;
constexpr uint16_t Edge = 0xB488;
constexpr uint16_t Text = 0xEF3A;
constexpr uint16_t Thread = 0x1082;
constexpr uint16_t Muted = 0x736C;
inline int CardWidth() { return ScreenW() * 68 / 100; }
inline int CardHeight() { return Font::BodyLineHeight() * 2 + 30; }
inline int Stride() { return CardWidth() + 20; }
inline int CenterY() { return ScreenH() / 2 + 6; }
}

// -----------------------------------------------------------------------------
// 链路 + 滚轮编辑页布局
// -----------------------------------------------------------------------------
namespace EditFlow {

// 428×142 下依次为：标签顶边 6、导轨 41、中心读数顶边 76、底部提示 114。
// 中部布局从上下区域的真实余量计算；弧面边项上抬后仍与导轨留开距离，底部不挤压提示。
inline int LinkY()    { return max(6, ScreenH() / 22); }
inline int DividerY() { return LinkY() + Font::BodyLineHeight() + max(12, Font::BodyLineHeight() * 3 / 4); }
inline int TipY()     { return ScreenH() - Font::SmallLineHeight() - 8; }
inline int DialY()    { return DividerY() + (TipY() - DividerY() - Font::BodyLineHeight()) / 2 + max(2, Font::BodyLineHeight() / 2 - 1); }
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

constexpr float MotionResponse = 26.0f;
constexpr float LayoutResponse = 22.0f;
inline int ArcLift() { return min(14, max(6, EditFlow::DialY() - EditFlow::DividerY() - 14)); }
inline int RailGap() { return 12; }
inline int BoxHeight()      { return max(Font::BodyLineHeight() + 6, ScreenH() / 7); }

}

// -----------------------------------------------------------------------------
// 顶部流程链路动画尺寸
// -----------------------------------------------------------------------------
namespace Link {

constexpr float MotionResponse = 22.0f;
constexpr float RopeVariation = 1.0f; // 有界的平滑随机起伏，局部波幅与交叉位置缓慢变化。
constexpr float RopeCycles = 3.0f; // 水平排开约三次起伏；摆幅直接使用主菜单的默认值，不再单独缩放。
constexpr float RopeResponse = 18.0f; // 牵引比数字滚轮略慢，换向保留惯性，停手后平滑收势。
constexpr float RopeTravelPerStep = 0.18f; // 每一格旋钮带动约 0.18 个绳波周期；与步骤标签位置无关。
inline int RailGap() { return EditFlow::DividerY() - EditFlow::LinkY() - Font::BodyLineHeight(); }

}

// -----------------------------------------------------------------------------
// 通用机械框线尺寸
// -----------------------------------------------------------------------------
namespace Frame {

inline int SelectionDepth() { return (ScreenW() >= 400) ? 3 : 2; }

inline int DividerCenterHalfGap() { return max(38, ScreenW() / 10); }
inline int DividerBevelW()        { return max(6,  ScreenW() / 70); }
inline int DividerBevelH()        { return max(4,  ScreenH() / 36); }
inline int CornerSize()           { return max(5,  ScreenW() / 90); }

}

} // namespace UITheme
