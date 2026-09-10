// 文件：src/net/includes/net/net_ntp.h
/* 【模块职责】执行一次不会直接改系统时钟的 NTP 基础校时。 */
#pragma once

#include <ctime>

using NetNtpAbortCheck = bool (*)();

/**
 * 在 WiFi 已连接时获取可信 UTC，并投递给 SysTime。
 * 本函数运行在 Core 0；settimeofday 和 RTC 写回仍由主循环中的 SysTime 完成。
 */
bool NetNtp_Sync(time_t &out_network_epoch, NetNtpAbortCheck is_abort_requested);
