// 文件：src/net/includes/net/net_outbox.h
/*
【模块职责】持久化保存“下次联网要做的事情”。

NetOutbox 只负责可靠存储和状态迁移，不打开 WiFi、不发 HTTP，也不解释业务载荷：
- 业务模块在用户操作完成后调用 Enqueue，把最小参数写入专用 NVS；
- NetTaskRegistry 在已建立并完成校时的联网会话中领取任务；
- 执行成功后 Complete，临时失败 Retry，不可恢复错误 DeadLetter。

【可靠性语义】
任务采用 at-least-once（至少一次）执行。若设备在服务器成功、Complete 尚未落盘时掉电，
下次联网会用同一个 job_id 重放，因此服务端必须把 job_id 当作幂等键。

【载荷约束】
这里只保存小命令或稳定索引。图片、大 JSON 和完整业务数据仍放在各自存储中，payload 只保存
定位符、版本号等最小信息。
*/
#pragma once

#include <Arduino.h>
#include <ctime>

constexpr size_t NET_OUTBOX_PAYLOAD_MAX = 384;
constexpr uint8_t NET_OUTBOX_CAPACITY = 24;

enum class NetOutboxJobState : uint8_t
{
    Pending = 0,
    DeadLetter = 1,
};

/** 对外任务快照；payload 编码由 task_id 对应的网络任务拥有。 */
struct NetOutboxJob
{
    uint64_t job_id = 0;
    uint64_t dedup_key = 0;
    uint32_t created_sequence = 0;
    int64_t not_before_epoch = 0;
    uint16_t task_id = 0;
    uint16_t payload_length = 0;
    uint8_t priority = 0;
    uint8_t attempt_count = 0;
    NetOutboxJobState state = NetOutboxJobState::Pending;
    uint8_t payload[NET_OUTBOX_PAYLOAD_MAX] = {};
};

enum class NetOutboxEnqueueResult : uint8_t
{
    Ok,
    AlreadyPending,
    InvalidArgument,
    StorageUnavailable,
    QueueFull,
    SaveFailed,
};

struct NetOutboxStats
{
    bool storage_ready = false;
    uint8_t pending_count = 0;
    uint8_t dead_letter_count = 0;
    uint8_t capacity = NET_OUTBOX_CAPACITY;
};

/** 初始化独立 outbox NVS 分区；通常由 NetService_Init() 统一调用。 */
bool NetOutbox_Init();

/** 返回只读统计，不触发联网。 */
NetOutboxStats NetOutbox_GetStats();

/**
 * 原子加入一个联网任务。
 *
 * dedup_key=0 表示不去重；非 0 时，同 task_id + dedup_key 的待处理任务只保留一份，
 * 并通过 out_job_id 返回已存在任务的稳定 ID。
 */
NetOutboxEnqueueResult NetOutbox_Enqueue(
    uint16_t task_id,
    const void *payload,
    uint16_t payload_length,
    uint8_t priority,
    uint64_t dedup_key,
    uint64_t *out_job_id = nullptr);

/** 领取已到期的最高优先级任务，并在返回前持久化 attempt_count+1。 */
bool NetOutbox_ClaimNextReady(int64_t now_epoch, NetOutboxJob *out_job);

/** 服务端已确认成功：原子删除任务。 */
bool NetOutbox_Complete(uint64_t job_id);

/** 临时失败：保留任务并设置最早重试 UTC；0 表示下次联网立即可重试。 */
bool NetOutbox_Retry(uint64_t job_id, int64_t not_before_epoch);

/** 永久失败或载荷不兼容：保留现场但不再自动执行。 */
bool NetOutbox_DeadLetter(uint64_t job_id);
