/* 【模块职责】独立注册广播/私人收件任务；共用实现，HTTP 在 Core 0，持久化在主循环。 */
#pragma once

#include <Arduino.h>

constexpr uint16_t NET_TASK_MAILBOX_RECEIVE = 6;
constexpr uint16_t NET_TASK_BROADCAST_RECEIVE = 7;

/** setup 时先注册广播再注册私人任务；注册顺序决定公共周期执行顺序，不改网络底层。 */
bool NetBroadcastReceive_Register();
bool NetMailboxReceive_Register();
