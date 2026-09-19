/*
【模块职责】持久化设备收件箱，并向提醒队列与统一日历提供主循环接口。

【线程边界】
- NetMailboxReceive 在 Core 0 只生成 SysMailboxIncomingMessage；
- SysMailbox_Accept、Update 与日历查询均在 Arduino 主循环调用；
- 网络核只允许读取已经落盘的连续 accepted_sequence，用于下一轮幂等 ACK。

【可靠性】只有一封消息完整写入 mailbox NVS 且连续游标同步提交后才算接收成功。
服务器 ACK 可以延迟到下一轮联网，因此掉电最多导致重复拉取，不会导致未落盘消息丢失。
*/
#pragma once

#include <Arduino.h>
#include <time.h>

constexpr uint8_t SYS_MAILBOX_CAPACITY = 24;
constexpr size_t SYS_MAILBOX_CONTENT_MAX = 384;

enum class SysMailboxMessageType : uint8_t
{
    InstantText = 0,
    ScheduledText = 1,
};

struct SysMailboxIncomingMessage
{
    uint64_t sequence = 0;
    SysMailboxMessageType type = SysMailboxMessageType::InstantText;
    uint16_t font_color = 0x07FF;
    int64_t trigger_epoch = 0;
    char message_id[37] = {};
    char sender_public_id[65] = {};
    char sender_alias[33] = {};
    char content[SYS_MAILBOX_CONTENT_MAX + 1] = {};
};

struct SysMailboxScheduledEvent
{
    uint8_t slot = 0;
    uint64_t sequence = 0;
    time_t trigger_epoch = 0;
};

enum class SysMailboxAcceptResult : uint8_t
{
    Accepted,
    AlreadyAccepted,
    SequenceGap,
    QueueFull,
    InvalidMessage,
    StorageUnavailable,
};

/** 初始化专用 mailbox NVS；不会联网，也不会修改其他分区。 */
bool SysMailbox_Init();

/** 主循环把网络线程移交的消息按连续 sequence 原子写入本地。 */
SysMailboxAcceptResult SysMailbox_Accept(const SysMailboxIncomingMessage &message);

/** 网络线程读取本机已经可靠保存的最高连续序号；不访问 NVS。 */
uint64_t SysMailbox_GetAcceptedSequence();

/**
 * 将本地连续接收游标与服务端已经确认的权威游标对齐。
 * 服务端领先时只推进后续接收基线，不删除本地仍待展示的旧消息；写入 NVS 失败返回 false。
 */
bool SysMailbox_ReconcileServerAcknowledged(uint64_t server_sequence);

/** 主循环按 sequence 把普通文本送入 SysReminder；队列满时保留较新的消息等待。 */
void SysMailbox_Update();

/** 日历重建时复制仍待触发的远程日期消息，不返回正文。 */
size_t SysMailbox_CopyScheduledEvents(SysMailboxScheduledEvent *output, size_t capacity);

/** 日历在休眠唤醒判断前查询是否存在到期远程日期消息。 */
bool SysMailbox_HasDueScheduled(time_t now);

/** 只允许 SysCalendar 调用：按时间、sequence 把到期消息送入提醒队列。 */
bool SysMailbox_ProcessDueScheduled(time_t now);

/** 提醒系统实际拉起弹窗后调用；删除本地记录并允许下一封进入提醒 FIFO。 */
void SysMailbox_ConfirmDispatched(uint64_t sequence);
