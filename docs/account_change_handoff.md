# Account 认证改造交接说明

> 用途：供后续对话继续修改 Prescript 固件和 Flutter App。本文记录当前服务器状态、已确认的产品方向和跨仓库改造边界。当前只完成评估，尚未实施账号改造。
>
> 更新时间：2026-09-20

## 1. 仓库位置

- 固件：`E:\develop\esp_projects\ESP32_Prescript_Terminal`
- Flutter App：`E:\develop\esp_projects\Prescript_Terminal_Mobile`
- 服务器：`E:\develop\esp_projects\Prescript_Terminal_Server`
- 测试域名：`https://test.prescript.cloud`

修改固件或 App 前，必须先阅读各自的 `maintain-prescript-*` skill，并搜索当前声明、实现和调用点；不要根据旧的 `命令集.txt` 猜接口。

## 2. 产品决定（已确认）

目标不是把后台审核密码改成邮箱登录，而是改造“设备首次绑定”这一套身份流程：

```text
邮箱注册/登录
  -> 手机 App 连接设备 BLE
  -> App 读取设备机器码/认领信息
  -> 当前账号直接认领设备（不让用户输入配对码）
  -> 服务器建立账号-设备归属
  -> 服务器签发设备永久凭据
  -> App 通过 BLE 把凭据写回设备
  -> 设备以后脱离手机，使用自己的凭据独立联网
```

用户层面不需要额外确认或二次验证；但协议内部仍需一次不可见的设备证明/认领握手，避免仅凭 HTTP 伪造机器码抢占设备。

最终有两个独立认证主体：

- `AccountPrincipal`：邮箱账号，用于 App 查看和管理名下设备。
- `DevicePrincipal`：设备永久凭据，用于设备联网、投稿、信箱、白名单和收发消息。

不要把邮箱、验证码或账号 Token 写入固件；设备永久凭据也不应在每次请求中直接作为 Bearer Token 使用，仍建议“永久凭据换短期 access token”。

## 3. 当前服务器真实状态

服务器已经有以下稳定能力：

- `devices`：机器码、公开 ID、状态、凭据版本、最近在线时间。
- `device_sessions`：设备凭据换短期 Bearer 会话。
- 自动绑定接口：
  - `POST /api/v1/device/binding/challenge`
  - `POST /api/v1/device/binding/complete`
  - `POST /api/v1/device/binding/ack`
  - `POST /api/v1/device/binding/rebind`
- 设备业务接口：`/api/v1/device/*`，包括投稿、别名、白名单、消息、信箱。
- 投稿审核：common/city、批量审核、拒绝原因、恢复、公开指令池退回。
- 日志和设备管理：`/logs/`、`/devices/`、`/device-simulator/`。
- 设备管理已经支持重置绑定、禁用/启用、查看设备投稿/信箱/白名单。

当前首次绑定仍是：设备用固件内的绑定主密钥生成 HMAC 证明，服务器根据机器码创建设备并返回 `device_key`。相关实现主要在：

- 服务器：`app/device_identity.py`、`app/api.py`
- 固件：`src/net/sources/services/net_device_binding.cpp`
- 固件身份存储：`src/sys/sources/sys_device_identity.cpp`
- 固件登录：`src/net/sources/net_auth_session.cpp`

服务器目前还有开发者/管理员密码后台认证。这次产品变更不要求删除或替换它们。

## 4. 当前固件状态

固件已经具备：

- 本地机器码生成和持久化。
- 身份记录中保存 `public_id`、`device_key`、凭据版本。
- `GET:IDENTITY` 公开身份响应（不返回 device key）。
- `DEV_BIND:<public_id>:<device_key>` 本地写入绑定凭据。
- 设备联网时用 `machine_code + device_key` 请求 `/api/v1/device/session`。
- 设备独立联网收信、确认信箱、发送消息和白名单相关流程。
- 设备主动重新绑定任务，以及禁用后服务器拒绝登录/绑定的处理。

重要文件：

- `src/sys/sources/sys_device_identity.cpp`
- `src/sys/sources/sys_protocol.cpp`
- `src/sys/sources/sys_router.cpp`
- `src/net/sources/services/net_device_binding.cpp`
- `src/net/sources/services/net_device_auth_prepare.cpp`
- `src/net/sources/services/net_mailbox_receive.cpp`
- `src/net/sources/net_auth_session.cpp`

第一版改造优先复用现有身份存储、`DEV_BIND` 和设备登录流程，不要同时重写信箱协议。

## 5. 当前 Flutter App 状态

App 已有：

- Android BLE 扫描、连接、服务发现、写入和通知。
- `BoundDevice` 设备模型，但真实云端账号/设备绑定仍未完成。
- `PrescriptApi`，目前主要用于公开指令池和测试投稿身份。
- 网络页面仍显示“机器码 + 设备通行码、无需注册账号”的旧说明。
- 云端投稿目前使用 `/submission-context` 发放随机测试 ID。

重要文件：

- `lib/core/ble/terminal_ble_client.dart`
- `lib/core/protocol/terminal_protocol.dart`
- `lib/app/terminal_controller.dart`
- `lib/core/cloud/prescript_api.dart`
- `lib/core/models/bound_device.dart`
- `lib/features/network/network_screen.dart`

App 目前没有账号登录、邮箱验证码、账号会话、设备认领界面，需要新增；不要把账号逻辑散落到 Widgets 中，网络认证应放在 cloud/auth 层，设备状态继续由 `TerminalController` 汇总。

## 6. 目标服务器改造（后续实施，不要本轮直接做）

新增数据模型：

```text
accounts
email_login_challenges
account_sessions
device_ownerships
device_claim_sessions
```

建议第一版规则：一个账号可拥有多台设备，一台设备只有一个 active owner。历史解绑/转让要保留记录。

新增账号 API（草案）：

```text
POST   /api/v1/account/email/request
POST   /api/v1/account/email/verify
GET    /api/v1/account/me
DELETE /api/v1/account/session
GET    /api/v1/account/devices
POST   /api/v1/account/device-claims
POST   /api/v1/account/device-claims/{claim_id}/complete
```

账号 API 和设备 API 分 namespace：

- `/account/*` 只接受账号会话，并检查账号是否拥有目标设备。
- `/device/*` 只接受设备 Bearer 会话，继续给固件使用。

账号代表设备投稿/发消息时，业务日志必须同时记录 `actor_account_id` 和 `subject_device_id`。信箱的“设备已可靠保存”ACK 仍只能由实体设备推进，手机查看信箱不能冒充设备 ACK。

## 7. 需要退役的旧机制

不要编辑或删除历史 Alembic migration；先新增迁移并双轨运行。稳定后再退役：

- 设备自动完成绑定的旧 challenge/complete/ack/rebind 流程。
- `device_binding_challenges` 表。
- `DEVICE_BINDING_MASTER_SECRET` 及固件共享绑定主密钥依赖。
- 服务器按 master secret 推导永久 `device_key` 的逻辑。
- 设备模拟器中的旧 proof 辅助接口。
- App/网页投稿中的随机测试 ID、`/submission-context` 和测试链路通行码（正式账号投稿启用后再删）。

第一阶段可以临时复用现有 HMAC 证明作为“App 已通过 BLE 接触设备”的内部握手，但最终应迁移到每台设备独立的出厂认领密钥；服务器随机生成设备永久凭据，不再依赖全局 credential master 推导。

## 8. 固件和 App 的实施顺序

1. 服务器先增加邮箱账号、验证码、账号会话；旧设备流程不动。
2. 服务器增加账号-设备归属和 claim session API。
3. App 增加邮箱登录、账号状态和设备列表。
4. 固件增加“接收 claim challenge、返回设备证明”的 BLE 协议；继续复用已有 `DEV_BIND` 写入凭据。
5. App 实现：连接 BLE -> 读取身份 -> 请求 claim -> 转发 challenge -> 完成认领 -> 写入永久凭据。
6. 设备首次独立联网，使用新凭据换短期 Token；验证投稿、信箱、白名单和重启恢复。
7. 改造模拟器，使其模拟“账号 + App + BLE + 设备联网”完整流程。
8. 最后再关闭旧自动绑定接口和测试身份入口。

## 9. 必须验证的场景

- 新账号注册并认领一台未归属设备。
- 同一账号认领多台设备。
- 已归属设备被第二个账号认领时必须拒绝。
- App 中断后重试，不重复创建设备、不生成重复 owner。
- 设备收到凭据后断电，重启仍能联网。
- 服务器重置设备凭据后，旧设备会话失效，App 能重新下发凭据。
- 设备禁用后不能联网、不能被新账号认领。
- 解绑/转让后旧账号不能查看新 owner 的信箱和白名单。
- 账号代设备投稿和发信的审计轨迹完整，设备直接联网业务仍正常。
- 邮箱验证码、账号 Token、设备永久凭据均不进入日志、截图或固件串口输出。

## 10. 交接提醒

这次改造的核心不是“用邮箱替换 machine_code”，而是增加一层：

```text
账号 = 人的身份与设备所有权
设备 = 硬件身份与独立联网凭据
```

保留 `device_id/public_id` 作为信箱、白名单和设备历史的稳定主键；账号只负责授权和管理。任何修改都要同时检查服务器 API、Flutter BLE/云端状态机和固件协议三方兼容性。
