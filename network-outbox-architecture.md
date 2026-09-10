# 定时联网与持久化 Outbox 架构

## 1. 结论

“下次联网要做的事情”使用独立的 `outbox` NVS 分区。它不放进以下位置：

- 默认 `nvs`：只有 20 KB，并与 WiFi、蓝牙和框架组件共享；
- `identity`：只保存不可随业务队列磨损、清空或迁移的设备凭据；
- LittleFS：仍保存配置、普通指令和备份，而且整包文件系统更新可能覆盖它；
- FATFS：会通过 USB MSC 暴露给电脑，且可能临时卸载；
- W25N01：当前只有原始 NAND 驱动，还没有 FTL、坏块管理和文件系统所有权；
- RTC 内存：断电后不能保证保留。

当前 16 MB Flash 尾部布局为：

```text
0xC10000 ┌──────────────────────────────┐
          │ LittleFS 3.75 MB             │
0xFD0000 ├──────────────────────────────┤
          │ outbox NVS 64 KB             │  联网任务
0xFE0000 ├──────────────────────────────┤
          │ identity NVS 64 KB           │  设备身份凭据
0xFF0000 ├──────────────────────────────┤
          │ coredump 64 KB                │
0x1000000└──────────────────────────────┘
```

Outbox 使用小而低频的固定记录，符合 NVS 的适用范围。当前每项最多 384 字节、最多 24 项；
较大对象仍由其业务存储拥有，任务只保存对象 ID、版本号或文件定位符。

## 2. 模块边界

### `SysNetworkOutbox`

只负责持久化状态机：

```text
不存在 ──Enqueue──> Pending ──Claim──> Pending(attempt+1)
                         ├──Complete──> 不存在
                         ├──Retry─────> Pending(not_before_epoch)
                         └──Permanent─> DeadLetter
```

它不依赖 WiFi、HTTP、UI、LittleFS 或具体服务器协议。主循环入队和 Core 0 消费通过互斥锁串行化。

### `SysNetwork`

负责“何时联网”和“联网期间做什么”：

1. 主循环把不可变的 `NetworkSessionRequest` 放入长度为 1 的 FreeRTOS 队列；
2. `NetDaemon` 在 Core 0 连接 WiFi；
3. 用独立 UDP NTP 获得可信 UTC，并把校时结果投递给 `SysTime`；
4. 从 Outbox 领取本轮到期任务，按 `job_type` 调用注册执行器；
5. 根据执行结果确认、退避或转入死信；
6. 完整同步再拉取原有隐秘指令 API；轻量校时跳过该 API；
7. 根据会话请求关闭 WiFi 或保持在线。

所有成功联网会话都会顺手消费 Outbox，而不只“完整同步”会话。每轮最多处理 8 项，避免积压任务
无限拉长射频开启时间。临时失败默认采用 30 秒起、最长 6 小时的指数退避；执行器也可返回服务端
指定的 `Retry-After`。

## 3. 可靠性语义

### 至少一次，不承诺恰好一次

领取任务时只持久化 `attempt_count+1`，任务仍是 Pending。服务器成功后才删除。若设备在这两个动作
之间掉电，下一次联网会再次执行同一个 `job_id`。因此：

- 设备不会因为“执行中”状态残留而永久丢任务；
- 同一操作可能到达服务器多次；
- 服务端必须按设备身份 + `job_id` 建唯一约束，并返回之前的成功结果。

不要在 ESP32 上通过“先标完成、再请求服务器”追求不重复，那会把重复问题变成无法恢复的丢失。

### 入队去重

`dedup_key=0` 表示每次用户操作都创建新任务。非 0 时，Outbox 对同一 `job_type + dedup_key` 的
Pending 任务返回 `AlreadyPending` 和原 `job_id`，适合防止按钮抖动或相同事件重复投递。

这里不实现“用新载荷覆盖旧载荷”。一个可能已经被服务器接收、但本地尚未确认的命令若被覆盖，
会破坏幂等语义。对于只关心最终结果的配置同步，应另建 `SyncState(domain, local_revision,
acked_revision, dirty)`，联网时读取最新业务事实，而不是把每次修改都变成命令。

### 损坏与迁移

- 记录包含 magic、schema、长度和校验值；损坏或不兼容时锁定 Outbox，保留现场；
- 首次从旧分区表升级时，新区域可能含旧 LittleFS 尾部，迁移标记只允许第一次格式化 `outbox`；
- 正式启用后若 NVS 初始化异常，不会自动擦除待办；
- 修改分区表会缩小 LittleFS。已有设备升级前必须确认要保留的 LittleFS 数据已经备份或具备迁移方案。

## 4. 业务接入模板

先给业务任务分配稳定的非零类型号，并定义紧凑、带版本的载荷：

```cpp
enum : uint16_t { JOB_UPLOAD_ACTION = 1001 };

struct __attribute__((packed)) UploadActionPayload
{
    uint8_t schema_version;
    uint32_t action_id;
    uint32_t local_revision;
};
```

业务初始化时注册 Core 0 执行器：

```cpp
static NetworkJobExecutionResult UploadAction(
    const SysNetworkJob &job,
    int64_t network_epoch)
{
    if (job.payload_length != sizeof(UploadActionPayload))
        return {NetworkJobDisposition::PermanentFailure, 0};

    UploadActionPayload payload = {};
    memcpy(&payload, job.payload, sizeof(payload));

    // HTTP 请求必须携带 job.job_id 作为幂等键，并设置有限超时。
    // 2xx/服务端“已处理” -> Complete
    // 超时、断网、429、5xx      -> Retry
    // 永久无效的 4xx/版本错误   -> PermanentFailure
    return {NetworkJobDisposition::Retry, 0};
}

void SysCloudAction_Init()
{
    Network_RegisterJobHandler(JOB_UPLOAD_ACTION, UploadAction);
}
```

用户操作产生一次不可丢的一次性事件时，把 Outbox 提交视为该云端动作被本机接受：

```cpp
UploadActionPayload payload = {1, action_id, local_revision};
uint64_t job_id = 0;
const SysNetworkEnqueueResult result = SysNetworkOutbox_Enqueue(
    JOB_UPLOAD_ACTION,
    &payload,
    sizeof(payload),
    100,                 // 优先级，数值越大越先执行
    action_id,           // 同一 action_id 防重复入队
    &job_id);
```

调用方必须处理 `QueueFull` 和 `SaveFailed`，不能仍向用户宣称“云端操作已排队”。任务入队本身不会立即
打开 WiFi，它等待开机同步、手动同步或周期校时形成的下一次联网会话。

## 5. 深度休眠接入点

以后增加深度休眠时，不需要复制一套网络业务表：

- `SysSleepCoordinator` 在睡前读取 Outbox 统计；
- 策略决定“有高优先级任务时先联网”还是“留到下一计划联网窗口”；
- 若决定联网，只请求一次 Network Session，并用现有 Network blocker 阻止立即入睡；
- 网络任务结束后释放 blocker，睡眠协调器继续执行“休眠前待办”；
- “休眠前待办”应使用另一张短生命周期表，不能塞进网络 Outbox，因为触发条件、失败策略和掉电语义不同。

这样持久化队列、联网策略、传输实现和休眠策略保持四个独立所有者。
