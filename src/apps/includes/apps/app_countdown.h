#pragma once
#include <Arduino.h>

// 【接口说明】从外部直接启动 TT2 倒计时；写入结束时间和完成后弹出的自定义指令文本。
void Countdown_Start(int min, int sec, const char* custom_cmd = nullptr);
bool Countdown_IsActive();
// 【接口说明】计算当前距离结束时间的剩余秒数，HUD 和倒计时页面共用这份全局状态。
int Countdown_GetRemainingSeconds();
