/*
【模块职责】统一提醒调度接口。

闹钟、日程等业务在到期时提交提醒；本服务只负责缓存同时到期的提醒，
并在主循环中逐条复用 PushNotify 展示。休眠截止时间由 SysSleepScheduler 独立管理。
*/
#pragma once

#include <Arduino.h>

/** 提醒内容类型；Custom 使用 text，Random 忽略 text 并抽取随机指令。 */
enum class SysReminderKind : uint8_t
{
    Custom,
    Random,
};

/**
 * 提交一条待展示提醒。
 * 调用者必须位于主循环；服务会复制 text，不保留调用方指针。
 * 队列满时返回 false，并保留已经排队的较早提醒。
 */
bool SysReminder_Submit(SysReminderKind kind, const char *text = nullptr, bool keep_stack = false);

/** 主循环消费提醒队列；PushNotify/Prescript 正在展示时会等待，不覆盖当前提醒。 */
void SysReminder_Update();
