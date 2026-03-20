// 文件：src/sys/sys_router.h
#pragma once
#include <Arduino.h>

// 处理蓝牙发来的杂乱字符串
void SysRouter_ProcessBLE(const String& msg);

// 处理网络 API 截获的隐秘指令
void SysRouter_ProcessAPI(uint32_t tt, const String& title, const String& text);