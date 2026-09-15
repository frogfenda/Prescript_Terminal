/* 【模块职责】注册会话认证准备与设置页强制在线验证任务，不承担首次绑定。 */
#pragma once

#include <Arduino.h>

constexpr uint16_t NET_TASK_DEVICE_AUTH_PREPARE = 3;
constexpr uint16_t NET_TASK_DEVICE_AUTH_VERIFY = 4;

bool NetDeviceAuthPrepare_Register();
