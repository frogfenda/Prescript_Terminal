# 定时联网 Outbox 框架交接文档

> 更新时间：2026-09-09  
> 项目：`E:\develop\esp_projects\ESP32_Prescript_Terminal`  
> 用途：供后续 Codex 对话或维护者直接接续“离线产生任务、下次联网执行”的开发。

## 0. 新对话开始时先做什么

1. 使用 `maintain-prescript-terminal` Skill，并完整读取其 `SKILL.md`。
2. 运行 `git status --short`。当前工作树包含用户尚未提交的设备身份、协议和网络改动，不能覆盖或回退。
3. 重新读取本文件以及以下源文件，源代码优先于本文：
   - `src/sys/includes/sys/sys_network_outbox.h`
   - `src/sys/sources/sys_network_outbox.cpp`
   - `src/sys/includes/sys/sys_network.h`
   - `src/sys/sources/sys_network.cpp`
   - `src/main.cpp`
   - `partitions_n16r8.csv`
4. 用 `rg` 搜索真实调用点，不能根据本文假定某个业务任务已经接入。

建议的开场核查命令：

```powershell
git status --short
rg -n "SysNetworkOutbox_Enqueue\(|Network_RegisterJobHandler\(" src
rg -n "SysNetworkOutbox_Init|_Network_DrainOutbox|NetworkSessionRequest" src
```

## 1. 当前结论和实现状态

已经完成的是通用基础设施：

- 独立 `outbox` NVS 分区；
- 固定容量、掉电可恢复的联网任务存储；
- 主循环到 Core 0 的不可变联网会话请求队列；
- `job_type -> NetworkJobHandler` 执行器注册表；
- 每次成功联网并取得 NTP 时间后自动消费 Outbox；
- 完成、临时重试、永久失败/死信三种结果；
- 固定 `job_id` 的 at-least-once 重放语义；
- APP 断网入口收回 `SysNetwork`，`AppWifiConnect` 不再直接修改 `g_state`。

当前没有完成的是具体业务接入：

- 没有任何业务代码调用 `SysNetworkOutbox_Enqueue()`；
- 没有任何业务模块调用 `Network_RegisterJobHandler()`；
- 因此当前 Outbox 正常启动、正常参与联网流程，但实际任务数为 0；
- 原有 NTP 校时和隐秘指令 API 仍是固定联网流程，不是 Outbox 任务。

后续对话不能把“框架已接通”描述成“业务同步已经可用”。

## 2. 存储布局和选择理由

当前 `partitions_n16r8.csv` 的尾部布局：

```text
0xC10000  LittleFS / spiffs   0x3C0000 = 3.75 MB
0xFD0000  outbox NVS          0x010000 = 64 KB
0xFE0000  identity NVS        0x010000 = 64 KB
0xFF0000  coredump            0x010000 = 64 KB
0x1000000 Flash 结束
```

Outbox 必须继续与以下存储隔离：

- 默认 `nvs`：只有 20 KB，并与 WiFi、蓝牙和框架组件共享；
- `identity`：只保存设备身份凭据，不能承受业务队列的磨损、清空或迁移；
- LittleFS：保存配置、普通指令和 `/Backup`，完整 LittleFS 镜像更新会覆盖该分区；
- FATFS：通过 USB MSC 暴露且可能被卸载，掉电一致性也不适合关键小队列；
- W25N01：当前只是原始 NAND BSP，没有 FTL、坏块管理和明确的文件系统所有权；
- RTC 内存：完全掉电后不能保证保留。

Outbox 当前每项最大 384 字节、最多 24 项。NVS 只保存小命令、稳定 ID、版本号或文件定位符，
不要存图片、大 JSON、完整业务数据库或频繁采样日志。

### 分区迁移警告

该分区表把 LittleFS 缩小到 3.75 MB。已有设备首次刷入新分区表前，如果 LittleFS 有必须保留的
配置、普通指令或备份数据，必须先导出或设计一次性迁移。只刷应用固件但不正确处理分区表与旧
LittleFS，不能宣称数据安全。

`SysNetworkOutbox_Init()` 对首次从旧 LittleFS 尾部划出的 `outbox` 区域允许一次自动格式化，并在
默认 NVS 写入 `sys_migrate/outbox_v1` 标记。标记存在后若专用 NVS 初始化失败，代码会保留现场，
不会静默擦除待办。

## 3. 模块所有权

### `SysNetworkOutbox`

文件：

- `src/sys/includes/sys/sys_network_outbox.h`
- `src/sys/sources/sys_network_outbox.cpp`

只拥有持久化状态机，不拥有 WiFi、HTTP、TLS、UI、业务对象或服务器协议。

主要接口：

```cpp
bool SysNetworkOutbox_Init();
SysNetworkOutboxStats SysNetworkOutbox_GetStats();

SysNetworkEnqueueResult SysNetworkOutbox_Enqueue(
    uint16_t job_type,
    const void *payload,
    uint16_t payload_length,
    uint8_t priority,
    uint64_t dedup_key,
    uint64_t *out_job_id = nullptr);

bool SysNetworkOutbox_ClaimNextReady(int64_t now_epoch, SysNetworkJob *out_job);
bool SysNetworkOutbox_Complete(uint64_t job_id);
bool SysNetworkOutbox_Retry(uint64_t job_id, int64_t not_before_epoch);
bool SysNetworkOutbox_DeadLetter(uint64_t job_id);
```

主循环可以入队，Core 0 网络任务可以领取/确认。所有 NVS 操作由模块内部 FreeRTOS mutex 串行化。

### `SysNetwork`

文件：

- `src/sys/includes/sys/sys_network.h`
- `src/sys/sources/sys_network.cpp`

拥有联网策略、会话请求、Core 0 网络任务、NTP、原有隐秘指令 API，以及 Outbox 执行器调度。

新增接口：

```cpp
using NetworkJobHandler = NetworkJobExecutionResult (*)(
    const SysNetworkJob &job,
    int64_t network_epoch);

bool Network_RegisterJobHandler(uint16_t job_type, NetworkJobHandler handler);
void Network_Disconnect();
```

联网会话使用长度为 1 的 `NetworkSessionRequest` FreeRTOS 队列，不再用两个跨核全局布尔量表达
`keep_alive` 和 `fetch_hidden_api`。当前一次只运行一个联网会话，不积压过时的重复联网请求。

## 4. 完整运行时序

```text
用户操作（离线也可）
    |
    | SysNetworkOutbox_Enqueue()
    v
outbox NVS: Pending
    |
    | 等待开机同步 / 手动同步 / 周期校时
    v
NetworkSessionRequest -> NetDaemon(Core 0)
    |
    +-> 连接 WiFi
    +-> UDP NTP 得到可信 UTC
    +-> SysTime_SubmitNetworkTime() 把时间交回主循环
    +-> _Network_DrainOutbox()
            |
            +-> ClaimNextReady(): attempt_count 先落盘 +1
            +-> 查找 job_type 对应 handler
            +-> handler 执行 HTTP/TLS
                    |
                    +-> Complete: 删除 NVS 记录
                    +-> Retry: 设置 not_before_epoch
                    +-> PermanentFailure: 标记 DeadLetter
    +-> 完整同步时继续拉取原有隐秘指令 API
    +-> 关闭 WiFi，或按 keep_alive 保持在线
```

任何成功完成 WiFi + NTP 的会话都会消费 Outbox，包括轻量周期校时。NTP 失败时不消费，因为 TLS、
重试时间和服务端时间判断通常都需要可信时间。

每轮最多执行 8 项，避免积压后一次联网无限消耗射频、电量和任务栈。默认临时失败退避为：

```text
30 秒 -> 60 秒 -> 120 秒 -> ... -> 最长 6 小时
```

执行器可以用 `retry_after_seconds` 覆盖默认值，例如处理 HTTP `Retry-After`。

## 5. 可靠性语义

### 5.1 至少一次执行

任务领取时仅持久化 `attempt_count + 1`，任务仍保持 `Pending`。服务器成功后才通过 `Complete()`
删除。因此设备在“服务器已成功、本地尚未删除”之间掉电时，下次联网会重放相同任务。

这是设计行为，不是缺陷。服务端必须：

- 接收设备身份和 `job_id`；
- 对“设备身份 + job_id”建立唯一约束或幂等记录；
- 重复请求返回第一次请求的成功结果，不能重复产生副作用。

不要在设备端先删任务再请求服务器，那会把“可能重复”变成“必然可能丢失”。

### 5.2 去重键

`dedup_key == 0`：每次调用都创建独立任务。

`dedup_key != 0`：如果已经存在相同 `job_type + dedup_key` 的 Pending 任务，返回
`AlreadyPending` 和原 `job_id`，不会再写一份。

不要用去重机制覆盖一个已经尝试过的命令载荷。服务器可能已处理旧载荷，只是本地尚未收到确认；
此时原地覆盖会破坏幂等语义。

### 5.3 命令 Outbox 与最终状态同步不是一回事

适合 Outbox 的任务：

- 上传一次已发生事件；
- 创建、删除或确认一个远端对象；
- 上报一条不可丢的有限记录；
- 请求服务器执行一次具有明确幂等键的动作。

不适合把每次变化都排入 Outbox 的任务：

- 音量、语言、用户资料等只关心最终值的配置；
- 高频计数器或可合并统计；
- 大型文件、图片或批量历史数据。

这类状态同步后续应增加独立的 `SysSyncState`：

```text
domain + local_revision + acked_revision + dirty
```

联网时读取业务层当前事实并上传最新版本，成功后推进 `acked_revision`。不要把十次离线修改发送十遍。

## 6. 新任务接入步骤

### 第一步：确认任务类型

先判断它是“一次性命令”还是“最终状态同步”。只有前者直接使用当前 Outbox。若需求不明确，不要
为了赶进度把最终状态硬编码成大量任务。

### 第二步：分配稳定的 `job_type`

在拥有该云端业务的 SYS 模块头文件中定义稳定、非零的类型号。不要在 APP 文件里散落魔法数字。

建议按业务域预留区间，例如：

```cpp
enum : uint16_t
{
    JOB_ACTION_UPLOAD = 1001,
    JOB_ACTION_DELETE = 1002,
};
```

一旦量产并产生持久任务，不得随意改变已有类型号含义。

### 第三步：定义紧凑、可版本化的 payload

```cpp
struct __attribute__((packed)) ActionUploadPayload
{
    uint8_t schema_version;
    uint32_t action_id;
    uint32_t local_revision;
};
```

约束：

- 最大 384 字节；
- 第一字段建议放 `schema_version`；
- 优先保存稳定 ID、revision 和定位符；
- 不保存 `String`、指针、STL 容器或含隐式 padding 的未约束对象；
- 不保存设备密钥。执行器需要认证时通过设备身份模块临时复制凭据；
- payload 结构变化时，旧版本必须能识别并迁移，或者明确转入死信。

### 第四步：实现并注册执行器

执行器运行在 Core 0 的 `NetDaemon` 中，此时 WiFi 已连接，`network_epoch` 是本轮 NTP 返回值。

```cpp
#include "sys/sys_network.h"
#include "sys/sys_network_outbox.h"
#include <cstring>

namespace
{
    constexpr uint16_t JOB_ACTION_UPLOAD = 1001;

    struct __attribute__((packed)) ActionUploadPayload
    {
        uint8_t schema_version;
        uint32_t action_id;
        uint32_t local_revision;
    };

    NetworkJobExecutionResult ExecuteActionUpload(
        const SysNetworkJob &job,
        int64_t network_epoch)
    {
        if (job.payload_length != sizeof(ActionUploadPayload))
            return NetworkJobExecutionResult(NetworkJobDisposition::PermanentFailure);

        ActionUploadPayload payload = {};
        memcpy(&payload, job.payload, sizeof(payload));
        if (payload.schema_version != 1)
            return NetworkJobExecutionResult(NetworkJobDisposition::PermanentFailure);

        /*
         * 在这里执行有限超时的 HTTP/TLS 请求。
         * 请求必须携带 job.job_id 作为幂等键；认证凭据不能写日志。
         */
        const int http_status = 503; // 示例占位，接入时替换为真实客户端调用。

        if (http_status >= 200 && http_status < 300)
            return NetworkJobExecutionResult(NetworkJobDisposition::Complete);
        if (http_status == 408 || http_status == 429 || http_status >= 500)
            return NetworkJobExecutionResult(NetworkJobDisposition::Retry);
        return NetworkJobExecutionResult(NetworkJobDisposition::PermanentFailure);
    }
}

void SysCloudAction_Init()
{
    if (!Network_RegisterJobHandler(JOB_ACTION_UPLOAD, ExecuteActionUpload))
        Serial.println("[云端动作] 联网待办执行器注册失败。");
}
```

在 `setup()` 中、`Network_Init()` 之前调用业务模块的初始化函数。所有执行器应在第一次联网会话可能
触发之前注册完毕。

执行器约束：

- 可以执行 WiFi 上的 HTTP/TLS；
- 每个网络调用必须设置有限超时；
- 不得直接操作 UI、导航、LittleFS 业务对象或 Wire/I2C；
- 如果响应需要修改本地业务数据，应投递一个固定容量结果到主循环，由业务模块在主循环落地；
- 不记录设备密钥、Authorization、完整敏感响应；
- HTTP 2xx 或服务端“该 job_id 已处理”映射为 `Complete`；
- 超时、断网、408、429、5xx 通常映射为 `Retry`；
- 明确不可恢复的载荷版本错误或永久 4xx 才映射为 `PermanentFailure`。

### 第五步：在用户操作入口入队

```cpp
ActionUploadPayload payload = {};
payload.schema_version = 1;
payload.action_id = action_id;
payload.local_revision = local_revision;

uint64_t job_id = 0;
const SysNetworkEnqueueResult result = SysNetworkOutbox_Enqueue(
    JOB_ACTION_UPLOAD,
    &payload,
    sizeof(payload),
    100,       // priority：数值越大越先执行
    action_id, // dedup_key：同一动作避免重复入队
    &job_id);
```

调用方必须逐项处理结果：

- `Ok`：本次云端动作已被本机可靠接受；
- `AlreadyPending`：已有相同待办，通常也可视为已接受；
- `QueueFull`：不能继续向用户宣称“已排队”，需要提示、降级或触发业务补偿；
- `SaveFailed` / `StorageUnavailable`：持久化没有成功，必须保留本地 dirty 状态或明确报错；
- `InvalidArgument`：代码或载荷错误，不能重试掩盖。

不要因为任务入队就立即调用 `Network_StartSync()`。本架构的默认语义是等待下一次既定联网窗口；
只有产品需求明确要求“立即同步”时，业务策略层才额外请求联网。

### 第六步：确认调用线程

`SysNetworkOutbox_Enqueue()` 内部有 mutex，存储本身可和 Core 0 消费串行化，但 NVS 提交仍是同步 Flash
操作。因此：

- 普通 APP 用户操作可在主循环业务路径入队；
- BLE/NFC 底层回调不要直接执行 NVS 写入，应沿现有事件/路由队列回到主循环后入队；
- 高频传感器回调、音频任务和 ISR 禁止直接入队；
- 如果同一用户动作还要修改 LittleFS，必须设计该业务自己的恢复/对账策略，因为 LittleFS 与 NVS
  之间不存在跨存储原子事务。

## 7. 当前联网触发条件

Outbox 不自己决定何时开 WiFi。当前联网窗口来自：

1. `Network_RequestBootSync(4000)`：启动后延迟完整同步；
2. `Network_StartSync()`：手动完整同步或 WiFi 配置完成后的同步；
3. `Network_StartTimeSyncOnly()`：周期轻量校时。

仅仅 Enqueue 一个任务不会额外安排联网。如果关闭周期校时、开机同步失败且用户也不手动同步，任务会
一直安全保留到下一次真正联网。

以后接深度休眠时，由 `SysSleepCoordinator` 决定是否因为高优先级 Outbox 在睡前补一次网络会话，
或直接保留到下一次计划唤醒。Outbox 不应直接调用休眠接口，也不应自己配置 RTC 唤醒。

## 8. 当前保护和容量策略

- 固定 24 个槽位，不动态扩容；
- 单任务 payload 最大 384 字节；
- 高 `priority` 先执行，同优先级按 `created_sequence` 先入先出；
- `job_id` 为持久随机 64 位 ID；
- 记录包含 magic、schema、record size、payload length 和校验值；
- 损坏或版本不兼容时锁定整个 Outbox，保留现场，不把损坏解释成“空队列”；
- 每次 Claim 先提交尝试次数，执行中掉电不会留下永久 InFlight 状态；
- 未注册 handler 的任务保留，并延后 1 小时；
- 死信占用槽位，可以用 `SysNetworkOutbox_Complete(job_id)` 明确删除，但目前没有死信管理 UI/命令。

如果未来实际任务量经常接近 24，不要简单扩大 payload 或无限加槽位。先检查是否把“最终状态”误建成
大量命令；高容量遥测应使用适合日志/流的存储，而不是 NVS Outbox。

## 9. 当前已知缺口

1. 没有具体 `job_type`、生产者或执行器。
2. 没有 `SysSyncState` 最终状态同步表。
3. 没有死信查询、导出、重试和删除的维护协议/UI。
4. 没有因为 Outbox 新任务自动安排下一次联网时间的策略模块。
5. 深度休眠协调器尚未实现。
6. `sys_network.cpp` 的编排、WiFi/NTP 传输和原有隐秘 API 仍在同一实现文件；公共边界已经建立，
   但如果网络业务继续增加，建议再拆为 Orchestrator、Transport 和各业务 Client。
7. 每项 handler 的超时依赖执行器自行设置；外层仍有原网络会话约 25 秒总超时兜底。
8. 尚未在真实硬件上验证首次分区迁移、断电重放、NVS 满队列和真实 HTTP 幂等。

## 10. 验证状态

2026-09-09 已完成：

- `platformio run -e esp32-s3`：成功；
- `platformio run -e esp32-s3 --target buildfs`：成功；
- 生成的分区二进制解析成功，分区连续且无重叠；
- 当前 `data/` 源文件约 156 KB，低于 3.75 MB LittleFS；
- 固件构建结果：静态 RAM 178,008 / 327,680 字节（54.3%）；
- 应用固件 1,858,441 / 2,097,152 字节（88.6%）；
- `git diff --check` 无补丁格式错误，仅有工作树行尾提示。

尚未完成：

- 烧录新分区表；
- 首次 outbox 格式化实机验证；
- 真实任务入队/重启恢复/联网发送；
- 在服务器成功、本地 Complete 前强制断电的幂等重放测试；
- QueueFull、损坏记录、死信和 HTTP 退避实机测试。

建议新增第一个业务任务后至少执行：

```powershell
pio run -e esp32-s3
pio run -e esp32-s3 --target buildfs
git diff --check
rg -n "SysNetworkOutbox_Enqueue\(|Network_RegisterJobHandler\(" src
```

实机测试矩阵：

1. 无 WiFi 入队，重启后数量不变；
2. WiFi 成功，服务端 2xx 后任务消失；
3. 服务端 500/超时，任务保留且 `not_before_epoch` 推后；
4. 服务端成功后、设备 Complete 前断电，重启后相同 `job_id` 重放且服务端不重复产生副作用；
5. 永久 4xx 转死信，不阻塞其他任务；
6. 连续加入 24 项后第 25 项返回 `QueueFull`；
7. 轻量 NTP 会话也能消费 Outbox；
8. 用户主动断网通过 `Network_Disconnect()` 收尾，休眠 blocker 最终释放。

## 11. 下一位维护者的推荐落地顺序

1. 让用户明确第一种真实任务、触发操作、服务器 URL、请求/响应、认证和幂等约定。
2. 判断它属于命令 Outbox 还是最终状态同步。
3. 在独立 SYS 业务模块定义稳定 `job_type` 和版本化 payload。
4. 实现有限超时、无 UI/I2C/LittleFS 副作用的 Core 0 handler。
5. 在 `Network_Init()` 之前注册 handler。
6. 在真实主循环业务提交点加入 Enqueue，并处理所有返回值。
7. 同时实现服务器端 `device + job_id` 幂等约束。
8. 编译、检查调用点，再按上面的断电/重试矩阵做实机验证。
9. 第一种任务稳定后，再考虑 `SysSyncState`、死信维护入口和深度休眠协调器。

## 12. 相关文件索引

- `NETWORK_OUTBOX_HANDOFF.md`：本交接文档。
- `network-outbox-architecture.md`：架构选择、机制和简要接入说明。
- `partitions_n16r8.csv`：`outbox` / `identity` 分区布局。
- `platformio.ini`：当前分区表与正式构建环境。
- `src/main.cpp`：`SysNetworkOutbox_Init()` 启动位置。
- `src/sys/includes/sys/sys_network_outbox.h`：持久化公开契约。
- `src/sys/sources/sys_network_outbox.cpp`：NVS 格式、互斥、恢复和状态迁移。
- `src/sys/includes/sys/sys_network.h`：handler 契约和网络公开接口。
- `src/sys/sources/sys_network.cpp`：会话队列、NTP、Outbox drain、退避和执行器注册表。
- `src/apps/sources/app_wifi_connect.cpp`：统一 `Network_Disconnect()` 的使用示例。
- `src/sys/includes/sys/sys_device_identity.h`：设备身份公开接口；网络认证接入时先读真实契约。
- `src/sys/sources/sys_device_identity.cpp`：独立 identity NVS，不能与 Outbox 合并。
