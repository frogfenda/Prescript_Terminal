# Net 服务与离线待办架构

## 结论

当前方案采用“联网会话编排 + 认证上下文 + 普通任务注册表 + 持久 Outbox”结构，适合设备只在固定窗口联网、
离线期间持续接收用户操作的场景。网络业务不会继续堆进一个状态机分支，持久化层也不感知 HTTP。

NTP 被定义为每次真实联网会话的基础阶段，不注册成普通任务，原因是：

- Outbox 的 `not_before_epoch` 和指数退避依赖可信 UTC；
- 未来 HTTPS 证书时间校验也需要可信时钟；
- NTP 失败时不应继续执行依赖时间的业务任务；
- 它建立的是会话运行条件，不是一次业务操作。

设备绑定与临时认证属于每次会话的 `SESSION_PREPARE` 阶段，固定在 Outbox 之前执行。隐秘指令拉取仍是
普通网络任务，但旧域名当前不可用，已退出自动触发，只保留显式任务入口等待迁移。

## 分层

```text
apps / 业务生产者
    |  发起会话                      |  离线操作产生持久任务
    v                                v
NetService                    NetOutbox（独立 NVS）
    | WiFi + NTP                     |
    | 绑定/临时 Token                |
    |                                | 下次联网领取
    +----------> NetTaskRegistry <---+
                       |
                       +-- 设备绑定与认证
                       +-- 隐秘指令拉取
                       +-- 后续上传、下载、状态上报等任务
```

代码目录与 `sys` 平行：

```text
src/net/
  includes/net/
    net_service.h
    net_ntp.h
    net_outbox.h
    net_task_registry.h
    net_builtin_tasks.h
    net_http_transport.h
    net_auth_session.h
    services/net_device_binding.h
    services/net_hidden_prescript.h
  sources/
    net_service.cpp
    net_ntp.cpp
    net_outbox.cpp
    net_task_registry.cpp
    net_builtin_tasks.cpp
    net_http_transport.cpp
    net_auth_session.cpp
    services/net_device_binding.cpp
    services/net_hidden_prescript.cpp
```

`net` 使用 `sys` 提供的配置、时间、休眠 blocker、命令结果和业务路由等基础能力；状态机本身不反向
嵌入具体网络业务。任务模块如果要修改日程、LittleFS 或 UI，必须通过自己的跨核邮箱，在
`NetTaskRegistry_Update()` 所在主循环落地。

## 一次联网的实际逻辑

```text
会话请求入队
  -> Core 0 唤醒
  -> 校验 WiFi 配置
  -> 连接 AP
  -> NTP 获取可信 UTC，并等待主线程把它真正写入系统时钟
  -> SESSION_PREPARE：未绑定则挑战应答并保存永久 Key；随后用 Key 换取本轮 RAM Token
  -> 消费本轮到期 Outbox（默认最多 8 项）
  -> 执行匹配 trigger_mask 的普通任务
  -> 执行请求显式列出的 task_ids
  -> 成功状态停留 2 秒
  -> 关闭 WiFi，或按 keep_alive 保持在线
```

主循环持续调用 `NetService_Update()`，只做四类轻量工作：

1. 调用每个任务的 `main_update` 落地跨核结果；
2. 到点触发开机标准同步；
3. 执行 60 秒会话总超时兜底；
4. 根据用户配置触发周期轻量校时。

## 四种任务形态

| 形态 | 是否持久化 | 执行时机 | 例子 |
|---|---:|---|---|
| 会话基础阶段 | 否 | 每次会话固定执行 | WiFi、NTP |
| 会话准备任务 | 否 | NTP 后、Outbox 前 | 设备绑定、临时认证 |
| 触发标签任务 | 否 | 匹配本轮 `trigger_mask` | 后续远程指令同步 |
| Outbox 任务 | 是 | 任意成功联网会话中到期后执行 | 后续用户操作上传、状态上报 |

显式 `task_ids` 适合某个页面临时请求一项服务；触发标签适合“每次标准同步都执行”的服务；
Outbox 适合离线期间已经发生、不能丢失且需要最终送达的业务事实。

## 状态机

`NetServiceState` 只表达会话阶段，不表达具体业务名：

- `Disconnected`：WiFi 关闭、无会话；
- `Connecting`：连接 AP；
- `SyncingTime`：执行必需 NTP；
- `RunningTasks`：消费 Outbox 或运行普通任务；
- `SyncSuccess`：会话成功，或手动保持在线；
- `ConnectFailed`：AP 连接失败；
- `SyncFailed`：NTP、总超时或强制中止。

因此以后增加十个 HTTP 命令，也不会增加十个网络状态。若 UI 未来需要显示具体任务名称，应增加只读
`current_task_id/current_task_name` 快照，而不是扩展状态枚举。

## 持久化选择

Outbox 使用分区表中的独立 64 KiB `outbox` NVS：

- 不和 LittleFS 业务文件的挂载、格式化及目录结构耦合；
- 固定 24 个槽位，单项 payload 最多 384 字节，RAM 和 Flash 上限明确；
- 每条记录带 magic、schema、record size 和校验值；
- mutex 串行化主循环入队与 Core 0 消费；
- 领取前先持久化 `attempt_count + 1`，执行中掉电仍可重放。

大文件和大 JSON 不应复制进 NVS。它们留在所属文件系统，Outbox payload 只保存稳定文件 ID、版本号、
内容哈希或业务主键。

## 可靠性语义

持久任务是 at-least-once，而不是 exactly-once：

```text
领取并增加 attempt_count
  -> 发请求
  -> 服务器成功
  -> 本地 Complete 删除
```

若设备在服务器成功后、删除前掉电，同一个 `job_id` 会再次发送。服务端必须将 `job_id` 用作幂等键。

执行结果有四种：

- `Complete`：删除任务；
- `Retry`：保留任务，使用执行器给出的秒数或默认指数退避；
- `PermanentFailure`：转入死信，保留现场，不再自动执行。
- `AuthBlocked`：凭据缺失或被拒绝；持久任务保留并暂停本轮认证队列，不进入死信。

任务若声明 `auth_requirement=DeviceSession`，注册表会在调用业务执行器前统一保证本轮 Token 可用。
业务执行器不读取永久 Key，Token 也不写入 Outbox 或身份分区。

## 自动绑定与 HTTPS 边界

固件只通过校验证书的 HTTPS 访问新服务端。NTP 结果必须先由主线程确认应用，之后 TLS 才能开始。
响应必须带 `Content-Length` 且不得超过调用方上限，避免不受控 JSON 占满内部堆。

未绑定设备用 eFuse 基础 MAC 生成 `PT-XXXXXXXXXXXX` 机器码，再按协议 v1 对服务器随机挑战执行
HMAC-SHA256。服务器返回的永久 Key 原子写入独立 `identity` NVS；正常联网只用该 Key 换取临时
Bearer Token。Token 仅存在于当前 WiFi 会话 RAM，断网、失败或中止都会清除。

测试固件通过构建环境变量 `PRESCRIPT_DEVICE_BINDING_MASTER_SECRET` 注入批次主密钥，脚本只在
`.pio` 构建目录生成临时头文件，不把秘密放进仓库或编译命令行。量产前必须改成工厂逐机秘密并开启
Flash Encryption/eFuse 保护；共享批次密钥不是最终量产安全方案。

未注册的 `task_id` 不会被删除，而是延后 1 小时，允许固件模块暂时缺失或升级后恢复。
已注册但未声明 `accepts_persistent` 的会话任务若被误写入 Outbox，会转入死信，避免在缺少幂等约束时
错误重放。

## 深度休眠扩展建议

“下次休眠前要做什么”可以复用注册表和结果模型，但不应直接复用网络 Outbox：

- 休眠前任务通常必须在几百毫秒内完成，不能无限 Retry；
- 它需要 `deadline`、`must_complete`、`can_skip` 等字段；
- 任务可能只需 RAM 队列，只有跨重启仍必须执行的项目才持久化；
- 执行环境在主循环，不能复用 Core 0 网络 I/O 约束。

建议未来新增平行的 `SleepService + SleepTaskRegistry`，保留相同的“注册/编排/存储分离”思想，
但使用独立任务定义和超时策略，不把两套生命周期强塞进一张通用表。
