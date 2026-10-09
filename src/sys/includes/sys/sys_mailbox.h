/*
【模块职责】持久化设备收件箱，并向提醒队列与统一日历提供主循环接口。

【线程边界】
- NetMailboxReceive 在 Core 0 只生成 SysMailboxIncomingMessage；
- SysMailbox_Accept、Update 与日历查询均在 Arduino 主循环调用；
- 网络核只允许读取各信箱已经落盘的 accepted_sequence，用于下一轮幂等 ACK。

【可靠性】只有消息和所属信箱游标提交到 mailbox NVS 后才算接收成功；广播允许服务端过滤造成的跳号。
服务器 ACK 可以延迟到下一轮联网，因此掉电最多导致重复拉取，不会导致未落盘消息丢失。
*/
#pragma once

#include <Arduino.h>
#include <time.h>

constexpr uint8_t SYS_MAILBOX_CAPACITY = 24;
/** 两路各预留一半容量；旧版超出额度的私信保留到正常展示，不在升级时删信。 */
constexpr uint8_t SYS_MAILBOX_CHANNEL_CAPACITY = SYS_MAILBOX_CAPACITY / 2;
constexpr size_t SYS_MAILBOX_CONTENT_MAX = 384;

/** 数值同时写入 NVS：0 为旧版私信，禁止调换。展示优先级不依赖枚举数值。 */
enum class SysMailboxKind : uint8_t
{
    Private = 0,
    Broadcast = 1,
};

enum class SysMailboxMessageType : uint8_t
{
    InstantText = 0,
    ScheduledText = 1,
};

struct SysMailboxIncomingMessage
{
    SysMailboxKind mailbox = SysMailboxKind::Private;
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
    SysMailboxKind mailbox = SysMailboxKind::Private;
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

/** 主循环保存消息与分渠道游标；私信要求连续，广播只要求递增。未落盘不推进 ACK。 */
SysMailboxAcceptResult SysMailbox_Accept(const SysMailboxIncomingMessage &message);

/** 网络线程读取指定信箱已经可靠保存的最高序号；不访问 NVS。 */
uint64_t SysMailbox_GetAcceptedSequence(SysMailboxKind mailbox);

/**
 * 将指定信箱的本地接收游标与服务端已经确认的权威游标对齐。
 * 服务端领先时只推进后续接收基线，不删除本地仍待展示的旧消息；写入 NVS 失败返回 false。
 */
bool SysMailbox_ReconcileServerAcknowledged(SysMailboxKind mailbox, uint64_t server_sequence);

/** 主循环优先处理已到期日期消息，再按可靠接收顺序分发即时消息；不抢占既有提醒。 */
void SysMailbox_Update();

/** 日历重建时复制仍待触发的远程日期消息，不返回正文。 */
size_t SysMailbox_CopyScheduledEvents(SysMailboxScheduledEvent *output, size_t capacity);

/** 日历在休眠唤醒判断前查询是否存在到期远程日期消息。 */
bool SysMailbox_HasDueScheduled(time_t now);

/** 主循环或日历调用：按时间、广播优先、渠道序号把到期消息送入提醒队列。 */
bool SysMailbox_ProcessDueScheduled(time_t now);

/** 提醒系统实际拉起弹窗后调用；删除本地记录并允许下一封进入提醒 FIFO。 */
void SysMailbox_ConfirmDispatched(SysMailboxKind mailbox, uint64_t sequence);
