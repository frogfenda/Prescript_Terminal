/* 倒计时应用业务接口：状态由 App 后台 tick 维护，页面/HUD 只读；复用 SYS 计时内核与提醒/唤醒。 */
#pragma once
#include <Arduino.h>
#include "sys/sys_duration_timer.h"
struct CountdownSnapshot
{
    SysDurationSnapshot timer;
    bool active;
    int minutes, seconds;
};
/** 合法范围 00:01..99:59；无效输入不影响当前会话，显式启动替换旧会话的未展示提示。 */
bool Countdown_TryStart(int minutes, int seconds, const char *text=nullptr);
/** 先结算当前时刻；仅运行/暂停中可取消，已经到期的完成事实继续投递，返回 false。 */
bool Countdown_Cancel();
void Countdown_Update(uint32_t now);
CountdownSnapshot Countdown_GetSnapshot();
// 保留外部启动与 HUD 查询接口；业务实现位于 app_countdown.cpp。
void Countdown_Start(int minutes, int seconds, const char *text=nullptr);
bool Countdown_IsActive();
int Countdown_GetRemainingSeconds();
