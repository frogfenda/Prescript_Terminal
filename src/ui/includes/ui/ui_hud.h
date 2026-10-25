/*
【模块职责】菜单 HUD 接口。绘制左侧状态栏并判断时间、电量、BUS、TMR 等状态是否需要重绘。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>

struct SysHudStatus
{
    bool nfc_active = false;
    int nfc_remaining_sec = 0;
    bool countdown_active = false;
    int countdown_remaining_sec = 0;
    bool pomodoro_active = false;
    bool pomodoro_rest = false;
    int pomodoro_remaining_sec = 0;
    uint8_t minute_bucket = 0;
};

// Draws the left HUD panel used by AppMenuBase and returns the panel width.
// 【接口说明】绘制菜单左侧 HUD：电池、标题、时间、BUS、TMR 与番茄专注/休息；状态行自动避让时间，返回左栏宽度。
int UIHud_DrawLeftPanel(const char* title_text);

/** 绘制电池图标；电量和充电闪烁状态来自 SysPower，只读状态不由 UI 修改。 */
void UIHud_DrawBatteryIcon(int32_t x, int32_t y);

/** 绘制命令同步的短时边框覆盖层；必须在页面完成本帧绘制后调用。 */
void UIHud_DrawSyncOverlay();

/** 获取 HUD 所需的 NFC、倒计时、番茄和分钟状态快照；只读，不推进业务。 */
SysHudStatus UIHud_GetRuntimeStatus();

/** 比较当前 HUD 状态与上一次快照，供菜单决定是否需要刷新。 */
bool UIHud_RuntimeStatusChanged();

// True when HUD content changed by second/minute ticks, NFC emulation, TT2 timer, etc.
// 【接口说明】比较当前 HUD 状态与上一帧缓存，时间、电量、NFC、倒计时变化时返回 true。
bool UIHud_NeedsRedraw();
