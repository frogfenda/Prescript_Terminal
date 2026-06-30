/*
【模块职责】菜单 HUD 接口。绘制左侧状态栏并判断时间、电量、BUS、TMR 等状态是否需要重绘。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>

// Draws the left HUD panel used by AppMenuBase and returns the panel width.
// 【接口说明】绘制菜单左侧 HUD：电池、标题、时间、BUS 伪装状态和 TMR 倒计时状态，并返回左栏宽度。
int UIHud_DrawLeftPanel(const char* title_text);

// True when HUD content changed by second/minute ticks, NFC emulation, TT2 timer, etc.
// 【接口说明】比较当前 HUD 状态与上一帧缓存，时间、电量、NFC、倒计时变化时返回 true。
bool UIHud_NeedsRedraw();
