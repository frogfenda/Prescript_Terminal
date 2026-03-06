#ifndef __SYS_TIME_H
#define __SYS_TIME_H
#include <Arduino.h>
#include <time.h>

// 初始化系统时间（配置默认时区）
void SysTime_Init();

// 核心接口 1：获取格式化好的时间字符串 (例如 "14:35")，直接用于 UI 渲染
void SysTime_GetTimeString(char* out_str);

// 核心接口 2：获取完整的底层时间结构体，方便以后的日历、闹钟应用提取年月日秒
bool SysTime_GetInfo(struct tm* out_info);

#endif