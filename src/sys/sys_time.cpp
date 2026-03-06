// 文件：src/sys/sys_time.cpp
#include "sys_time.h"

void SysTime_Init() {
    // 【系统级保障】：开机第一时间强行设定系统时区为东八区 (北京时间)
    // 这样哪怕一辈子不连网，硬件时钟也会从 08:00 开始走，而不是 00:00
    setenv("TZ", "CST-8", 1);
    tzset();
}

void SysTime_GetTimeString(char* out_str) {
    struct tm timeinfo;
    // 不再拦截 1970 年！有什么时间就显示什么时间
    if (getLocalTime(&timeinfo, 10)) {
        snprintf(out_str, 10, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        // 兜底防御：万一硬件时钟卡死
        strcpy(out_str, "08:00");
    }
}

bool SysTime_GetInfo(struct tm* out_info) {
    // 给日历、时钟等高级应用留的底层数据接口
    return getLocalTime(out_info, 10);
}