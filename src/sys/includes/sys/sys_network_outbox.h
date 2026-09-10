// 文件：src/sys/includes/sys/sys_network_outbox.h
/*
【模块职责】持久化保存“下次联网要做的事情”。

Outbox 只负责可靠存储和状态迁移，不负责打开 WiFi、发 HTTP 或解释业务载荷：
- APP/SYS 业务模块在用户操作完成后调用 Enqueue，把最小业务参数写入专用 NVS；
- SysNetwork 在一次联网会话建立后 Claim 任务，并交给已注册的业务执行器；
- 服务器成功后 Complete；临时失败 Retry；不可恢复错误 DeadLetter。

【可靠性语义】
任务执行采用 at-least-once（至少一次）语义。设备可能在服务器成功、Complete 尚未提交时掉电，
所以下次联网会用同一个 job_id 重放。服务器接口必须把 job_id 当作幂等键。

【载荷约束】
这里只保存“小命令/索引”，不要把图片、大 JSON 或完整业务数据库塞入 NVS。较大内容应继续放在
其所属文件系统中，payload 只保存稳定定位符和版本号。
*/
#pragma once

#include <Arduino.h>
#include <ctime>

constexpr size_t SYS_NETWORK_JOB_PAYLOAD_MAX = 384;
constexpr uint8_t SYS_NETWORK_OUTBOX_CAPACITY = 24;

enum class SysNetworkJobState : uint8_t
{
    Pending = 0,
    DeadLetter = 1,
};

/** 对外任务快照；payload 的编码由 job_type 对应的业务执行器拥有。 */
struct SysNetworkJob
{
    uint64_t job_id = 0;
    uint64_t dedup_key = 0;
    uint32_t created_sequence = 0;
    int64_t not_before_epoch = 0;
    uint16_t job_type = 0;
    uint16_t payload_length = 0;
    uint8_t priority = 0;
    uint8_t attempt_count = 0;
    SysNetworkJobState state = SysNetworkJobState::Pending;
    uint8_t payload[SYS_NETWORK_JOB_PAYLOAD_MAX] = {};
};

enum class SysNetworkEnqueueResult : uint8_t
{
    Ok,
    AlreadyPending,
    InvalidArgument,
    StorageUnavailable,
    QueueFull,
    SaveFailed,
};

struct SysNetworkOutboxStats
{
    bool storage_ready = false;
    uint8_t pending_count = 0;
    uint8_t dead_letter_count = 0;
    uint8_t capacity = SYS_NETWORK_OUTBOX_CAPACITY;
};

/** 初始化独立 outbox NVS 分区；应在配置加载后、Network_Init() 前调用一次。 */
bool SysNetworkOutbox_Init();

/** 返回只读统计，不触发联网。 */
SysNetworkOutboxStats SysNetworkOutbox_GetStats();

/**
 * 原子加入一个联网任务。
 *
 * dedup_key=0 表示不去重；非 0 时，同 job_type + dedup_key 的待处理任务只保留一份，
 * 并通过 out_job_id 返回已存在任务的稳定 ID。
 */
SysNetworkEnqueueResult SysNetworkOutbox_Enqueue(
    uint16_t job_type,
    const void *payload,
    uint16_t payload_length,
    uint8_t priority,
    uint64_t dedup_key,
    uint64_t *out_job_id = nullptr);

/**
 * 领取当前到期的最高优先级任务，并在返回前持久化 attempt_count+1。
 * 任务仍保持 Pending，掉电后会再次被领取；now_epoch 必须是本轮 NTP 得到的可信 UTC 时间。
 */
bool SysNetworkOutbox_ClaimNextReady(int64_t now_epoch, SysNetworkJob *out_job);

/** 服务器已经确认成功：原子删除任务。 */
bool SysNetworkOutbox_Complete(uint64_t job_id);

/** 临时失败：保留任务，并设置最早重试 UTC；0 表示下次联网立即可重试。 */
bool SysNetworkOutbox_Retry(uint64_t job_id, int64_t not_before_epoch);

/** 永久失败或载荷不兼容：保留现场但不再自动执行。 */
bool SysNetworkOutbox_DeadLetter(uint64_t job_id);
