/*
【模块职责】倒计时/番茄钟共用的持续时长状态机；不拥有时钟、页面、配置、提醒或唤醒设备。
【调用】主循环传入单调 uint32 毫秒；每段时长不超过 INT32_MAX，安全跨 millis 回绕。
【完成语义】Advance 只结算一次，到期事实保留到 Acknowledge；暂停/取消先结算，不能抹掉刚到期事件。
*/
#pragma once
#include <stdint.h>
#include <limits.h>

enum class SysDurationState : uint8_t { Idle, Running, Paused, Completed };
/** 显示秒数向上取整，剩余不足一秒时仍显示 1，只有真正到期才显示 0。 */
inline uint32_t SysDurationDisplaySeconds(uint32_t remaining_ms) { return (remaining_ms+999U)/1000U; }
struct SysDurationSnapshot
{
    SysDurationState state;
    uint32_t total_ms, remaining_ms, deadline_ms, revision;
    bool completion_pending;
};

class SysDurationTimer
{
    SysDurationState state_ = SysDurationState::Idle;
    uint32_t total_ = 0, remaining_ = 0, deadline_ = 0, revision_ = 0;
    bool pending_ = false;
public:
    /** 显式建立新会话；无效时长不修改旧状态。替换旧会话的通知策略由上层服务决定。 */
    bool start(uint32_t duration_ms, uint32_t now)
    {
        if (!duration_ms || duration_ms > (uint32_t)INT32_MAX) return false;
        total_ = remaining_ = duration_ms; deadline_ = now + duration_ms;
        state_ = SysDurationState::Running; pending_ = false; ++revision_;
        return true;
    }
    bool advance(uint32_t now)
    {
        if (state_ != SysDurationState::Running || (int32_t)(now-deadline_) < 0) return false;
        remaining_ = 0; state_ = SysDurationState::Completed; pending_ = true; ++revision_;
        return true;
    }
    /** 暂停/继续/取消返回是否改变运行状态；pause/cancel 先结算到期，重复操作不重复发完成事件。 */
    bool pause(uint32_t now)
    {
        advance(now);
        if (state_ != SysDurationState::Running) return false;
        remaining_ = deadline_-now; state_ = SysDurationState::Paused; ++revision_;
        return true;
    }
    bool resume(uint32_t now)
    {
        if (state_ != SysDurationState::Paused) return false;
        deadline_ = now+remaining_; state_ = SysDurationState::Running; ++revision_;
        return true;
    }
    bool cancel(uint32_t now)
    {
        advance(now);
        if (state_ != SysDurationState::Running && state_ != SysDurationState::Paused) return false;
        state_ = SysDurationState::Idle; remaining_ = 0; pending_ = false; ++revision_;
        return true;
    }
    /** 上层已保存/投递完成事实后确认；保留 Completed 状态供快照查询。 */
    void acknowledge() { if (pending_) { pending_ = false; ++revision_; } }
    /** 只读查询，不推进/确认状态；即使页面反复读取也不会消费完成事实。 */
    SysDurationSnapshot snapshot(uint32_t now) const
    {
        const uint32_t remain = state_ == SysDurationState::Running ?
            ((int32_t)(now-deadline_) >= 0 ? 0 : deadline_-now) : remaining_;
        return {state_,total_,remain,deadline_,revision_,pending_};
    }
};
