/*
【模块职责】统一提醒队列与 PushNotify 串行展示。
【线程约束】所有接口都由 Arduino 主循环调用，因此不引入额外锁和后台任务。
*/
#include "sys/sys_reminder.h"
#include "sys/app_manager.h"
#include "sys/sys_constants.h"
#include "sys/sys_mailbox.h"
#include "sys/resources/sys_specials.h"

namespace
{
    constexpr size_t MAX_PENDING_REMINDERS =
        PrescriptConst::MAX_SCHEDULES + PrescriptConst::MAX_ALARMS + 3;

    struct PendingReminder
    {
        PrescriptContent snapshot;
        bool keep_stack = false;
        uint64_t mailbox_sequence = 0;
        SysMailboxKind mailbox = SysMailboxKind::Private;
        SysClockReminderSource source = SysClockReminderSource::None;
        uint64_t event_id = 0;
    };

    PendingReminder s_pending[MAX_PENDING_REMINDERS];
    size_t s_head = 0;
    size_t s_count = 0;
    bool s_queueFullReported = false;
}

static SysOpResult Submit(
    SysReminderKind kind,
    const char *text,
    bool keep_stack,
    uint16_t font_color,
    uint64_t mailbox_sequence,
    SysMailboxKind mailbox,
    SysClockReminderSource source,
    uint64_t event_id,
    const char *title)
{
    if (event_id && source!=SysClockReminderSource::None)
        for (size_t i=0;i<s_count;++i) {
            const auto &item=s_pending[(s_head+i)%MAX_PENDING_REMINDERS];
            if (item.source==source && item.event_id==event_id) return SysOpResult::Ok("QUEUED");
        }
    if (s_count >= MAX_PENDING_REMINDERS)
    {
        // 满队列时收件主循环会重试；一次持续满载只报告一次，不能按每帧速度刷屏。
        if (!s_queueFullReported)
            Serial.println("[提醒-警告] 提醒队列已满，本次到期提醒无法入队。");
        s_queueFullReported = true;
        return SysOpResult::Error("QUEUE_FULL");
    }

    // 先确认有空位，再复制全部属性/抽取。选择和提交分开，排队失败不会消耗人物进度。
    PrescriptContent snapshot; PrescriptSelection selection;
    if (kind == SysReminderKind::Random) {
        if (!sysSpecials.selectRandom(snapshot, selection)) return SysOpResult::Error("OUT_OF_MEMORY");
    } else if (!SysPrescriptCatalog::Custom(appManager.getLanguage(), text, font_color, title, snapshot)) return SysOpResult::Error("OUT_OF_MEMORY");
    if (!sysSpecials.commit(selection)) return SysOpResult::Error("SAVE_FAILED");
    const size_t tail = (s_head + s_count) % MAX_PENDING_REMINDERS;
    auto &pending = s_pending[tail];
    pending.snapshot = std::move(snapshot);
    pending.keep_stack = keep_stack;
    pending.mailbox_sequence = mailbox_sequence;
    pending.mailbox = mailbox;
    pending.source = source;
    pending.event_id = event_id;
    ++s_count;
    return SysOpResult::Ok("QUEUED");
}

bool SysReminder_Submit(SysReminderKind kind,const char *text,bool keep_stack,uint16_t font_color,
                        uint64_t sequence,SysMailboxKind mailbox,const char *title)
{ return SysReminder_SubmitResult(kind,text,keep_stack,font_color,sequence,mailbox,title).ok(); }

SysOpResult SysReminder_SubmitResult(SysReminderKind kind,const char *text,bool keep_stack,uint16_t font_color,
                                   uint64_t sequence,SysMailboxKind mailbox,const char *title)
{ return Submit(kind,text,keep_stack,font_color,sequence,mailbox,SysClockReminderSource::None,0,title); }

bool SysReminder_SubmitClock(SysClockReminderSource source,uint64_t event_id,
    SysReminderPresentation presentation,SysReminderKind kind,const char *text)
{
    return Submit(kind,text,presentation==SysReminderPresentation::ReturnToPrevious,0x07FF,0,
                  SysMailboxKind::Private,source,event_id,nullptr).ok();
}

void SysReminder_CancelClock(SysClockReminderSource source)
{
    if (source==SysClockReminderSource::None) return;
    size_t kept=0;
    // 在环形队列的逻辑顺序内向前压缩，保持其他提醒的 FIFO 和邮箱确认数据。
    for (size_t read=0;read<s_count;++read) {
        const size_t at=(s_head+read)%MAX_PENDING_REMINDERS;
        if (s_pending[at].source==source) continue;
        const size_t target=(s_head+kept++)%MAX_PENDING_REMINDERS;
        if (target!=at) s_pending[target]=std::move(s_pending[at]);
    }
    for (size_t i=kept;i<s_count;++i) s_pending[(s_head+i)%MAX_PENDING_REMINDERS]=PendingReminder{};
    s_count=kept;
}

void SysReminder_Update()
{
    if (s_count == 0 || appManager.isCurrent(AppId::PushNotify) || appManager.isCurrent(AppId::Prescript))
        return;

    PendingReminder item = std::move(s_pending[s_head]);
    s_pending[s_head] = PendingReminder{};
    s_head = (s_head + 1) % MAX_PENDING_REMINDERS;
    --s_count;
    s_queueFullReported = false;

    SysPrescriptCatalog::Present(std::move(item.snapshot));
    PushNotify_Trigger_Prepared(item.keep_stack);

    /* 只有弹窗已经取得正文以后才删除本地邮件；更早掉电只会在重启后重复，不会漏信。 */
    if (item.mailbox_sequence != 0)
        SysMailbox_ConfirmDispatched(item.mailbox, item.mailbox_sequence);
}
