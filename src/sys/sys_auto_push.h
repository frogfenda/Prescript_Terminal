#ifndef __SYS_AUTO_PUSH_H
#define __SYS_AUTO_PUSH_H
#include <Arduino.h>

// 【手机预留接口】：未来通过蓝牙/局域网发指令，直接调用此函数即可！
void SysAutoPush_UpdateConfig(bool enable, uint32_t min_m, uint32_t max_m);

void SysAutoPush_Init();
void SysAutoPush_Update();
void SysAutoPush_ResetTimer();

#endif