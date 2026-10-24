/*
【模块职责】命令行队列实现。固定槽位的环形队列，用 FreeRTOS 自旋锁保护，不在入队路径上分配堆内存。
【重要约束】蓝牙回调和 NFC 任务都可能入队，临界区里只做 memcpy，不打印日志、不调用其他模块。
*/
#include "sys/sys_ble_queue.h"
#include <freertos/FreeRTOS.h>
#include <string.h>

namespace
{
    constexpr uint8_t kQueueSlots = PrescriptConst::MAX_CMD_QUEUE;
    constexpr uint8_t kRejectSlots = 4;

    portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

    SysCmdLine s_lines[kQueueSlots];
    uint8_t s_head = 0;
    uint8_t s_count = 0;

    SysCmdRejectNotice s_rejects[kRejectSlots];
    uint8_t s_reject_head = 0;
    uint8_t s_reject_count = 0;

    // 调用方必须已持有 s_lock。拒收通知本身也满时丢弃最旧的一条，只影响错误回包，不影响已排队的请求。
    void AppendRejectLocked(SysCmdSource source, SysCmdReject reason, const char *head, size_t length, uint32_t session)
    {
        if (s_reject_count == kRejectSlots)
        {
            s_reject_head = (s_reject_head + 1) % kRejectSlots;
            --s_reject_count;
        }
        SysCmdRejectNotice &notice = s_rejects[(s_reject_head + s_reject_count) % kRejectSlots];
        notice.source = source;
        notice.session = session;
        notice.reason = reason;
        const size_t copy = length < sizeof(notice.head) - 1 ? length : sizeof(notice.head) - 1;
        memcpy(notice.head, head, copy);
        notice.head[copy] = '\0';
        ++s_reject_count;
    }
}

bool SysCmdQueue_Push(SysCmdSource source, const char *text, size_t length, uint32_t session)
{
    if (text == nullptr)
        return false;
    if (length >= PrescriptConst::CMD_LINE_MAX)
    {
        SysCmdQueue_Reject(source, SysCmdReject::TooLong, text, length, session);
        return false;
    }

    bool accepted = false;
    portENTER_CRITICAL(&s_lock);
    if (s_count < kQueueSlots)
    {
        SysCmdLine &slot = s_lines[(s_head + s_count) % kQueueSlots];
        slot.source = source;
        slot.session = session;
        slot.length = static_cast<uint16_t>(length);
        memcpy(slot.text, text, length);
        slot.text[length] = '\0';
        ++s_count;
        accepted = true;
    }
    else
    {
        AppendRejectLocked(source, SysCmdReject::Busy, text, length, session);
    }
    portEXIT_CRITICAL(&s_lock);
    return accepted;
}

void SysCmdQueue_Reject(SysCmdSource source, SysCmdReject reason, const char *head, size_t length, uint32_t session)
{
    portENTER_CRITICAL(&s_lock);
    AppendRejectLocked(source, reason, head ? head : "", head ? length : 0, session);
    portEXIT_CRITICAL(&s_lock);
}

bool SysCmdQueue_Pop(SysCmdLine &out)
{
    bool found = false;
    portENTER_CRITICAL(&s_lock);
    if (s_count > 0)
    {
        const SysCmdLine &slot = s_lines[s_head];
        out.source = slot.source;
        out.session = slot.session;
        out.length = slot.length;
        memcpy(out.text, slot.text, slot.length + 1);
        s_head = (s_head + 1) % kQueueSlots;
        --s_count;
        found = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return found;
}

bool SysCmdQueue_PopReject(SysCmdRejectNotice &out)
{
    bool found = false;
    portENTER_CRITICAL(&s_lock);
    if (s_reject_count > 0)
    {
        out = s_rejects[s_reject_head];
        s_reject_head = (s_reject_head + 1) % kRejectSlots;
        --s_reject_count;
        found = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return found;
}

void SysCmdQueue_DropSource(SysCmdSource source)
{
    portENTER_CRITICAL(&s_lock);
    // 按原顺序压缩保留其他来源的请求，只移除指定来源。
    uint8_t kept = 0;
    for (uint8_t i = 0; i < s_count; ++i)
    {
        const uint8_t from = (s_head + i) % kQueueSlots;
        if (s_lines[from].source == source)
            continue;
        const uint8_t to = (s_head + kept) % kQueueSlots;
        if (to != from)
            s_lines[to] = s_lines[from];
        ++kept;
    }
    s_count = kept;
    // 拒收回包同样属于旧连接，不能留给下一个客户端。
    kept = 0;
    for (uint8_t i = 0; i < s_reject_count; ++i)
    {
        const uint8_t from = (s_reject_head + i) % kRejectSlots;
        if (s_rejects[from].source == source)
            continue;
        const uint8_t to = (s_reject_head + kept) % kRejectSlots;
        if (to != from)
            s_rejects[to] = s_rejects[from];
        ++kept;
    }
    s_reject_count = kept;
    portEXIT_CRITICAL(&s_lock);
}

void SysCmdLineAssembler::feed(const uint8_t *data, size_t length, uint32_t session)
{
    for (size_t i = 0; i < length; ++i)
    {
        const char c = static_cast<char>(data[i]);
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            if (overflow_)
                SysCmdQueue_Reject(source_, SysCmdReject::TooLong, buffer_, used_, session);
            else if (used_ > 0)
                SysCmdQueue_Push(source_, buffer_, used_, session);
            used_ = 0;
            overflow_ = false;
            continue;
        }
        // 超长后继续吞到换行为止；buffer_ 保留开头，供错误回包取出“组.动词#编号”。
        if (used_ + 1 >= PrescriptConst::CMD_LINE_MAX)
        {
            overflow_ = true;
            continue;
        }
        buffer_[used_++] = c;
    }
}

void SysCmdLineAssembler::reset()
{
    used_ = 0;
    overflow_ = false;
}
