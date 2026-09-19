/* 【模块职责】注册设备收件箱拉取任务；HTTP/JSON 在 Core 0，持久化在主循环。 */
#pragma once

#include <Arduino.h>

constexpr uint16_t NET_TASK_MAILBOX_RECEIVE = 6;

bool NetMailboxReceive_Register();
