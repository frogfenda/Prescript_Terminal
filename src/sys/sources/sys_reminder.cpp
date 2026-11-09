/*
【模块职责】统一内容请求的接收、链条提交和快照投递；计时/邮箱只提供事实和确认标识。
【线程约束】所有接口仅由Arduino主循环串行调用，不在状态提交和交接之间回调业务或分配文字。
*/
#include "sys/sys_reminder.h"
#include "sys/app_manager.h"
#include "sys/sys_constants.h"
#include "sys/resources/sys_specials.h"

namespace
{
    constexpr size_t MAX_PENDING_REMINDERS = PrescriptConst::MAX_SCHEDULES + PrescriptConst::MAX_ALARMS + 3;
    struct PendingReminder
    {
        PrescriptContent snapshot;
        SysReminderPresentation presentation = SysReminderPresentation::Interrupt;
        uint64_t mailbox_sequence = 0;
        SysMailboxKind mailbox = SysMailboxKind::Private;
        SysClockReminderSource source = SysClockReminderSource::None;
        uint64_t event_id = 0;
    };
    PendingReminder s_pending[MAX_PENDING_REMINDERS];
    size_t s_head = 0, s_count = 0;
    bool s_queueFullReported = false;
    PrescriptContent s_presented;

    // 导航和邮件确认共用一个出口；内容已取得完整所有权后才进入页面。
    void Deliver(PrescriptContent &&snapshot, SysReminderPresentation presentation,
                 uint64_t sequence, SysMailboxKind mailbox)
    {
        s_presented = std::move(snapshot);
        PushNotify_Trigger_Prepared(presentation == SysReminderPresentation::ReturnToPrevious);
        if (sequence) SysMailbox_ConfirmDispatched(mailbox, sequence);
    }
}

SysOpResult SysReminder_Submit(const PrescriptRequest &request, const DeliveryOptions &options)
{
    if ((options.delivery != SysReminderDelivery::Queue && options.delivery != SysReminderDelivery::Immediate &&
         options.delivery != SysReminderDelivery::CurrentPage) ||
        (options.presentation != SysReminderPresentation::Interrupt && options.presentation != SysReminderPresentation::ReturnToPrevious) ||
        static_cast<unsigned>(options.source) > static_cast<unsigned>(SysClockReminderSource::Schedule) ||
        (options.event_id && options.source == SysClockReminderSource::None) ||
        (options.delivery == SysReminderDelivery::CurrentPage &&
         (options.mailbox_sequence || options.source != SysClockReminderSource::None)))
        return SysOpResult::Error("INVALID_OPTIONS");
    if (request.source != SysPrescriptSource::Content && request.source != SysPrescriptSource::Random &&
        request.source != SysPrescriptSource::Special) return SysOpResult::Error("INVALID_SOURCE");

    if (options.event_id)
        for (size_t i = 0; i < s_count; ++i) {
            const auto &item = s_pending[(s_head + i) % MAX_PENDING_REMINDERS];
            if (item.source == options.source && item.event_id == options.event_id) return SysOpResult::Ok("QUEUED");
        }
    if (options.delivery == SysReminderDelivery::Queue && s_count >= MAX_PENDING_REMINDERS) {
        // 满队列的来源可在主循环重试；不复制、不抽取、不推进，也不逐帧刷日志。
        if (!s_queueFullReported) Serial.println("[提醒-警告] 提醒队列已满，本次到期提醒无法入队。");
        s_queueFullReported = true; return SysOpResult::Error("QUEUE_FULL");
    }

    PrescriptContent snapshot; PrescriptSelection selection;
    SysOpResult accepted = SysOpResult::Ok(options.delivery == SysReminderDelivery::Queue ? "QUEUED" :
        options.delivery == SysReminderDelivery::CurrentPage ? "PREPARED" : "PRESENTED");
    switch (request.source) {
    case SysPrescriptSource::Content: {
        const auto result = SysPrescript::Prepare(request.content, appManager.getLanguage(), snapshot);
        if (!result.ok()) return result;
        break;
    }
    case SysPrescriptSource::Random:
        if (!sysSpecials.selectRandom(snapshot, selection)) return SysOpResult::Error("OUT_OF_MEMORY");
        break;
    case SysPrescriptSource::Special: {
        if (!request.special_id) return SysOpResult::Error("EMPTY_ID");
        String id(request.special_id);
        if (id.length() != strlen(request.special_id)) return SysOpResult::Error("OUT_OF_MEMORY");
        id.trim(); if (id.isEmpty()) return SysOpResult::Error("EMPTY_ID");
        const bool known = sysSpecials.hasID(id);
        if (!sysSpecials.selectForced(id, snapshot, selection)) return SysOpResult::Error("OUT_OF_MEMORY");
        // 包括结果细节在内的文字均在commit前准备，状态保存后只有移动/导航。
        accepted = known ? SysOpResult::Ok(options.delivery == SysReminderDelivery::Queue ? "QUEUED" : "TRIGGERED", id)
                         : SysOpResult::Warn("NOT_FOUND", id);
        break;
    }
    }
    if (!sysSpecials.commit(selection)) return SysOpResult::Error("SAVE_FAILED");

    if (options.delivery == SysReminderDelivery::Queue) {
        auto &pending = s_pending[(s_head + s_count) % MAX_PENDING_REMINDERS];
        pending.snapshot = std::move(snapshot); pending.presentation = options.presentation;
        pending.mailbox_sequence = options.mailbox_sequence; pending.mailbox = options.mailbox;
        pending.source = options.source; pending.event_id = options.event_id;
        ++s_count;
    } else if (options.delivery == SysReminderDelivery::CurrentPage) {
        s_presented = std::move(snapshot);
    } else {
        Deliver(std::move(snapshot), options.presentation, options.mailbox_sequence, options.mailbox);
    }
    return accepted;
}

void SysReminder_CancelClock(SysClockReminderSource source)
{
    if (source == SysClockReminderSource::None) return;
    size_t kept = 0;
    // 按环形队列的逻辑顺序压缩，保留其它来源的FIFO和邮箱确认信息。
    for (size_t read = 0; read < s_count; ++read) {
        const size_t at = (s_head + read) % MAX_PENDING_REMINDERS;
        if (s_pending[at].source == source) continue;
        const size_t target = (s_head + kept++) % MAX_PENDING_REMINDERS;
        if (target != at) s_pending[target] = std::move(s_pending[at]);
    }
    for (size_t i = kept; i < s_count; ++i) s_pending[(s_head + i) % MAX_PENDING_REMINDERS] = PendingReminder{};
    s_count = kept;
}

void SysReminder_Update()
{
    if (!s_count || appManager.isCurrent(AppId::PushNotify) || appManager.isCurrent(AppId::Prescript)) return;
    PendingReminder item = std::move(s_pending[s_head]); s_pending[s_head] = PendingReminder{};
    s_head = (s_head + 1) % MAX_PENDING_REMINDERS; --s_count; s_queueFullReported = false;
    Deliver(std::move(item.snapshot), item.presentation, item.mailbox_sequence, item.mailbox);
}

// 展示内容与任何库的编辑/重载隔离，借用只持续到下一次成功展示。
const PrescriptContent &SysReminder_Presented() { return s_presented; }
