// 文件：src/net/includes/net/services/net_hidden_prescript.h
/* 【模块职责】注册并实现云端隐秘指令拉取这一项普通网络任务。 */
#pragma once

#include <Arduino.h>

constexpr uint16_t NET_TASK_HIDDEN_PRESCRIPT_PULL = 1;

/** 向 NetTaskRegistry 注册隐秘指令任务。 */
bool NetHiddenPrescript_Register();
