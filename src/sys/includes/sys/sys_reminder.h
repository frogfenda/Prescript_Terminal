/*
【模块职责】统一提醒调度接口。

闹钟、日程等业务在到期时提交提醒；本服务只负责缓存同时到期的提醒，
并在主循环中逐条复用 PushNotify 展示。休眠截止时间由 SysSleepScheduler 独立管理。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_mailbox.h"

/** 提醒内容类型；Custom 使用 text，Random 忽略 text 并抽取随机指令。 */
enum class SysReminderKind : uint8_t
{
    Custom,
    Random,
};

/** 页面中断是明确的业务策略；ReturnToPrevious 在接收提醒后返回原页面，Interrupt 清空原导航栈。 */
enum class SysReminderPresentation : uint8_t { Interrupt, ReturnToPrevious };
enum class SysClockReminderSource : uint8_t { None, Countdown, Pomodoro, Alarm, Schedule };
/**
 * 时钟业务提交入口。event_id 非零时对排队中的同来源同事件去重，队列满返回 false 供服务重试。
 * 主循环调用，文本复制；展示仍复用原队列/PushNotify，不能从页面销毁回调直接触发导航。
 */
bool SysReminder_SubmitClock(SysClockReminderSource source,uint64_t event_id,
    SysReminderPresentation presentation,SysReminderKind kind,const char *text=nullptr);
/** 显式取消/替换会话时移除该来源尚未展示的提示；不撤销已展示内容，不影响邮箱或其它来源。 */
void SysReminder_CancelClock(SysClockReminderSource source);

/**
 * 提交一条待展示提醒。
 * 调用者必须位于主循环；服务会复制 text，不保留调用方指针。
 * 队列满时返回 false，并保留已经排队的较早提醒。
 * mailbox_sequence 非零时，mailbox 与 sequence 一起标识待删除邮件；普通本地提醒保持默认值。
 */
bool SysReminder_Submit(
    SysReminderKind kind,
    const char *text = nullptr,
    bool keep_stack = false,
    uint16_t font_color = 0x07FF,
    uint64_t mailbox_sequence = 0,
    SysMailboxKind mailbox = SysMailboxKind::Private);

/** 主循环消费提醒队列；PushNotify/Prescript 正在展示时会等待，不覆盖当前提醒。 */
void SysReminder_Update();
