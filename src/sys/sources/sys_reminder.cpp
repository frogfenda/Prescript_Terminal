/*
【模块职责】统一提醒队列与 PushNotify 串行展示。
【线程约束】所有接口都由 Arduino 主循环调用，因此不引入额外锁和后台任务。
*/
#include "sys/sys_reminder.h"
#include "sys/app_manager.h"
#include "sys/sys_constants.h"

namespace
{
    constexpr size_t MAX_PENDING_REMINDERS =
        PrescriptConst::MAX_SCHEDULES + PrescriptConst::MAX_ALARMS + 3;

    struct PendingReminder
    {
        SysReminderKind kind = SysReminderKind::Custom;
        String text;
        bool keep_stack = false;
    };

    PendingReminder s_pending[MAX_PENDING_REMINDERS];
    size_t s_head = 0;
    size_t s_count = 0;
}

bool SysReminder_Submit(SysReminderKind kind, const char *text, bool keep_stack)
{
    if (s_count >= MAX_PENDING_REMINDERS)
    {
        Serial.println("[提醒-警告] 提醒队列已满，本次到期提醒无法入队。");
        return false;
    }

    size_t tail = (s_head + s_count) % MAX_PENDING_REMINDERS;
    s_pending[tail].kind = kind;
    s_pending[tail].text = text ? text : "";
    s_pending[tail].keep_stack = keep_stack;
    ++s_count;
    return true;
}

void SysReminder_Update()
{
    if (s_count == 0 || appManager.isCurrent(AppId::PushNotify) || appManager.isCurrent(AppId::Prescript))
        return;

    PendingReminder item = s_pending[s_head];
    s_pending[s_head].text = "";
    s_head = (s_head + 1) % MAX_PENDING_REMINDERS;
    --s_count;

    if (item.kind == SysReminderKind::Random)
        PushNotify_Trigger_Random(item.keep_stack);
    else
        PushNotify_Trigger_Custom(item.text.c_str(), item.keep_stack);
}
