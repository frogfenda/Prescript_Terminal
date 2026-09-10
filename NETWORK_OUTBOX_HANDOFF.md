# Net 服务接入与联网任务交接

本文供后续对话或维护者直接接手当前网络架构。架构设计说明见
`network-outbox-architecture.md`。

## 当前状态

迁移前完整工作树已提交为：

```text
ba1cf11 接入设备身份与持久化联网待办框架
```

该提交之后，网络代码已从 `src/sys` 迁入和它平行的 `src/net`。当前已完成固件全量编译验证。

目前接入的网络工作包括：

1. NTP：每次联网会话必经的基础阶段，不是普通任务；
2. 设备绑定与认证：`task_id=2`，每次会话在 Outbox 前自动执行；
3. HTTPS JSON 传输：证书校验、响应上限和会话剩余时限集中管理；
4. 隐秘指令拉取：普通任务，`task_id=1`，因旧域名失效暂不自动触发；
5. Outbox 消费器：框架已运行，但还没有业务生产者调用 `NetOutbox_Enqueue()`。

开机标准同步、设置页手动同步、WiFi 配置后同步和周期轻量校时都会先完成绑定/临时认证，并顺带消费
已经到期的 Outbox。旧隐秘指令任务只能由显式 `task_id` 调用，待服务端新接口完成后再恢复自动同步。

## 关键文件

- `src/net/includes/net/net_service.h`：会话请求、状态和外部入口；
- `src/net/sources/net_service.cpp`：Core 0 会话状态机、WiFi 生命周期、启动与周期策略；
- `src/net/includes/net/net_task_registry.h`：普通任务执行契约；
- `src/net/sources/net_task_registry.cpp`：注册、触发任务、显式任务和 Outbox 分发；
- `src/net/includes/net/net_outbox.h`：持久任务公开 API；
- `src/net/sources/net_outbox.cpp`：独立 NVS 记录格式和状态迁移；
- `src/net/sources/net_ntp.cpp`：基础 NTP 实现；
- `src/net/sources/net_http_transport.cpp`：受信根、HTTPS JSON、响应和超时上限；
- `src/net/sources/net_auth_session.cpp`：永久 Key 换本轮 RAM Token；
- `src/net/sources/net_builtin_tasks.cpp`：内置任务安装清单；
- `src/net/sources/services/net_device_binding.cpp`：自动绑定和认证准备任务；
- `src/net/sources/services/net_hidden_prescript.cpp`：普通任务范例；
- `src/main.cpp`：`NetService_Init/RequestBootSync/Update` 接入点。

## 不可破坏的边界

网络任务的 `execute` 固定在 Core 0，可执行 WiFi、HTTP、TLS 和 DNS，但必须遵守：

- 每个网络操作设置有限超时；
- 不直接修改 UI、APP 栈、LittleFS 业务对象或 Wire/I2C；
- 需要落地的数据先写入任务自己的线程安全邮箱；
- 在 `main_update` 中从邮箱取出并落地；
- 持久任务请求必须把 `invocation.job_id` 发给服务端作为幂等键。

`NetService_Update()` 已在主循环调用 `NetTaskRegistry_Update()`，任务模块不需要自行修改 `main.cpp`。

## 新增普通网络任务

### 1. 分配稳定 task_id

在任务公开头文件定义 ID。已占用：

```text
1 = NET_TASK_HIDDEN_PRESCRIPT_PULL
2 = 设备绑定与认证准备任务（内部保留，不得写入 Outbox）
```

ID 一旦用于持久化就不能改含义，也不能被其他任务复用。建议每个业务模块在自己的头文件维护 ID 和
版本化 payload。

### 2. 实现执行器

```cpp
#include "net/net_task_registry.h"

namespace
{
    NetTaskExecutionResult ExecuteUpload(
        const NetTaskInvocation &invocation,
        const NetTaskContext &context)
    {
        // invocation.persistent=true 时，HTTP 请求必须携带 invocation.job_id。
        // 校验 payload_length 和 payload_version；不兼容时返回 PermanentFailure。
        // 网络暂时失败返回 Retry；成功且服务端已确认才返回 Complete。
        return {NetTaskDisposition::Complete, 0};
    }

    void MainUpdate()
    {
        // 可选：只在主循环落地 Core 0 返回的数据。
    }
}
```

`context.network_epoch` 来自本轮 NTP；`context.RemainingMs()` 是本轮剩余预算。业务任务必须在预算不足时
拒绝启动新的阻塞请求。

### 3. 注册任务

```cpp
bool NetUpload_Register()
{
    NetTaskDefinition definition;
    definition.task_id = NET_TASK_EXAMPLE_UPLOAD;
    definition.name = "示例上传";
    definition.execute = ExecuteUpload;
    definition.main_update = MainUpdate;
    definition.accepts_persistent = true;
    definition.auth_requirement = NetTaskAuthRequirement::DeviceSession;

    // 如果每次标准同步都应执行，打开下面一行；仅由 Outbox 触发则保持 NONE。
    definition.trigger_mask = NET_TASK_TRIGGER_STANDARD_SYNC;
    return NetTaskRegistry_Register(definition);
}
```

只有显式设置 `accepts_persistent=true` 的任务才会执行 Outbox 调用；其他任务若被误入队会转入死信。
然后只在 `src/net/sources/net_builtin_tasks.cpp` 的 `NetBuiltinTasks_RegisterAll()` 增加注册调用。
不要修改 `net_service.cpp` 的状态机。

### 4. 选择触发方式

随标准同步执行：给定义增加 `NET_TASK_TRIGGER_STANDARD_SYNC`。

由某个页面临时点名执行：

```cpp
NetSessionRequest request;
request.task_count = 1;
request.task_ids[0] = NET_TASK_EXAMPLE_UPLOAD;
NetService_StartSession(request);
```

离线操作必须最终送达：写入 Outbox，不立即强制联网。

```cpp
struct __attribute__((packed)) UploadPayloadV1
{
    uint8_t version;
    uint32_t local_record_id;
};

UploadPayloadV1 payload = {1, local_record_id};
uint64_t job_id = 0;
const NetOutboxEnqueueResult result = NetOutbox_Enqueue(
    NET_TASK_EXAMPLE_UPLOAD,
    &payload,
    sizeof(payload),
    100,           // priority，数值越大越先执行
    local_record_id, // dedup_key；0 表示不去重
    &job_id);
```

业务提交成功应以 `Ok` 或 `AlreadyPending` 为准。`QueueFull`、`StorageUnavailable` 和 `SaveFailed`
必须反馈给调用者，不能假装已经排队。

## Outbox 载荷规则

- payload 上限 384 字节，队列总容量 24；
- 第一字段建议始终是 payload 版本；
- 只存发送所需的最小快照或稳定定位符；
- 如果原业务记录之后允许删除，应存足够的不可变快照；
- 如果服务端需要最新状态，可存业务 ID + revision，再由执行器读取业务存储；
- `dedup_key` 只合并同一 `task_id + dedup_key` 的 Pending 项；
- 任务被领取后仍保持 Pending，掉电后会重放。

## 会话 API

常用入口：

```cpp
NetService_Init();
NetService_RequestBootSync(4000);
NetService_Update();

NetService_StartStandardSync(false); // NTP + 绑定/认证 + Outbox + STANDARD_SYNC，结束断网
NetService_StartStandardSync(true);  // 同上，但保持在线
NetService_StartTimeSyncOnly();      // NTP + Outbox，不执行额外会话任务
NetService_Disconnect();
```

所有会话都强制先执行 NTP，然后运行 `SESSION_PREPARE` 建立设备身份上下文。不要创建“NTP task_id”，
也不要从业务执行器直接读取永久 Key 或自行维护 Token。

## 状态与 UI

UI 只判断通用阶段：

```cpp
NetServiceState state = NetService_GetState();
```

业务任务统一映射为 `RunningTasks`。如果以后要显示“正在上传日志”等细节，应给注册表增加线程安全的
当前任务只读快照，不能为每个业务继续扩展 `NetServiceState`。

## 故障与重试

- WiFi 失败：`ConnectFailed`，周期策略退避 5 分钟；
- NTP 失败：`SyncFailed`，本轮不执行任何普通任务；
- 绑定/认证不可用：本轮汇总为失败，认证型 Outbox 返回 `AuthBlocked` 并保留；
- 会话任务失败：只记录，等待下一轮策略触发，不在原地循环；
- 持久任务 `Retry`：默认从 30 秒指数退避，最多 6 小时；
- 未注册持久任务：保留并延后 1 小时；
- 持久任务 `PermanentFailure`：进入死信；
- 单轮最多消费 8 个 Outbox，总会话有 60 秒兜底。

## 验证命令

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3 -t buildfs
git diff --check
rg -n "sys_network|SysNetwork|Network_Start|Network_Update" src
```

实机至少验证：

1. 无真实 WiFi 配置时不开射频、不反复重试；
2. 开机 4 秒后标准同步；
3. NTP 成功后才出现 `RunningTasks`；
4. 未绑定设备完成挑战应答、保存身份并成功获取临时 Token；
5. 已绑定设备只执行 Key -> Token，不再次调用绑定接口；
6. 手动保持在线后可用同一入口断开；
7. Outbox 成功、临时失败、认证阻塞、掉电重放和死信路径；
8. 会话结束或中止后 Token 被清除且休眠 blocker 释放。

## 当前未完成项

- 尚无真实业务生产者调用 `NetOutbox_Enqueue()`；
- 尚无死信查看/重放/删除命令和 UI；
- 尚无 Outbox 满载告警；
- 旧隐秘指令域名返回错误，任务已暂停自动触发；需要迁移到新服务端认证 API；
- 测试阶段仍使用批次绑定主密钥；量产前需要逐机秘密、制造登记和 Flash Encryption；
- 深度休眠前任务尚未实现，应新建独立 `SleepService/SleepTaskRegistry`，不要复用网络 Outbox。
