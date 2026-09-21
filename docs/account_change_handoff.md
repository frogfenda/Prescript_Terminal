# Account 认证改造交接说明

> 用途：交给后续负责 Prescript Flutter App、ESP32 固件和服务器联调的对话。
> 本文只描述已经核对过的接口、当前实现和下一步跨仓库契约，不把规划内容误写成已完成能力。
>
> 更新时间：2026-09-22

## 1. 三个仓库与当前提交

- 固件：`E:\develop\esp_projects\ESP32_Prescript_Terminal`
- Flutter App：`E:\develop\esp_projects\Prescript_Terminal_Mobile`
- 服务器：`E:\develop\esp_projects\Prescript_Terminal_Server`
- 测试地址：`https://test.prescript.cloud`
- 服务器本轮设备认证改造提交：`193a565 Add account-linked device session exchange`

修改 App 或固件前必须读取对应的 `maintain-prescript-mobile-app`、
`maintain-prescript-terminal` skill，检查工作区状态，并以当前源代码和真实调用点为准。
不要继续根据旧的 `命令集.txt` 猜协议。

## 2. 产品身份边界

```text
账号 = 人的身份、投稿者、设备所有权和管理入口
设备 = 硬件身份、独立联网执行端和可追溯来源
```

- 一个账号可以拥有多台设备。
- 一台设备同时只有一个 active 账号归属。
- 账号会话和设备会话是两种不同的 Bearer Token，不能互换。
- 普通 App/网页账号操作只发送账号 Token，不附带机器码。
- 设备独立联网只发送机器码和设备长期凭证，换取短期设备 Token；设备 Token
  的服务端会话同时保存 `account_id` 和 `device_id`，因此日志可以追溯到账号和设备。
- 设备永久凭证、账号 Token、邮箱验证码和绑定证明都不能写日志、截图或串口。

## 3. 服务器当前真实接口

### 3.1 账号会话

```http
POST /api/v1/auth/register/request
POST /api/v1/auth/register/verify
POST /api/v1/auth/login
POST /api/v1/auth/login/email/request
POST /api/v1/auth/login/email/verify
GET  /api/v1/auth/me
DELETE /api/v1/auth/session
```

返回的 `access_token` 是账号 Token。账号 API 的认证头为：

```http
Authorization: Bearer <account_access_token>
```

### 3.2 账号主导的绑定接口（App 应使用）

下面四个接口位于 `/api/v1/auth`，都不是旧设备链路：

```http
GET  /api/v1/auth/devices
POST /api/v1/auth/devices/binding/challenge
POST /api/v1/auth/devices/binding/complete
DELETE /api/v1/auth/devices/{device_id}
```

请求和响应：

```json
// challenge
{"machine_code":"PT-001122AABBCC"}

// challenge response
{
  "protocol_version": 1,
  "challenge_id": "uuid",
  "challenge": "url-safe-random-text",
  "expires_at": "2026-09-22T00:00:00Z"
}

// complete
{
  "machine_code":"PT-001122AABBCC",
  "challenge_id":"uuid",
  "verification_code":"url-safe-hmac-proof"
}

// complete response
{
  "protocol_version": 1,
  "public_id":"ID-XXXXXXXXXXXXXXX",
  "device_key":"<只返回这一次的长期凭证>",
  "credential_version": 1,
  "confirmation_required": true
}
```

服务器只保存 `credential_hash`，不保存 `device_key` 原文。`device_key` 只能由
App 暂时接收并通过 BLE 写入设备；不能写入 App 普通日志或云端本地备份。

### 3.3 设备独立认证接口（固件应使用）

```http
POST /api/v1/auth/devices/session
```

请求：

```json
{"machine_code":"PT-001122AABBCC","device_key":"<设备保存的长期凭证>"}
```

响应：

```json
{
  "access_token":"<短期 bearer token>",
  "token_type":"bearer",
  "expires_at":"2026-09-23T00:00:00Z"
}
```

这个入口不需要邮箱、密码或账号 Token。签发时服务器会查找当前 active 的
`AccountDevice`，把账号和设备写进 `DeviceSession` 及业务审计。

设备认证失败时，除了 JSON `detail`，还会返回稳定响应头：

| HTTP | `X-Device-Error-Code` | 固件行为 |
|---|---|---|
| 401 | `INVALID_DEVICE_CREDENTIALS` | 保留本地凭证，按认证失败处理 |
| 401 | `DEVICE_UNBOUND` | 清除本地长期凭证，进入未绑定状态 |
| 403 | `DEVICE_DISABLED` | 保留现场但停止绑定/联网，等待 App 或管理端处理 |
| 401 | 无/会话过期 | 只清 RAM 短期 Token，保留长期凭证并下次重试 |

重要：服务器当前 `DEVICE_CHAIN_ENABLED=false`。旧 `/api/v1/device/session` 以及
依赖旧 `require_device_session` 的设备业务接口仍会返回 410；不要让新固件继续请求旧入口。
等固件和 App 完成联调后，再单独开放需要的设备业务接口。

### 3.4 账号解绑语义

```http
DELETE /api/v1/auth/devices/{device_id}
Authorization: Bearer <account_access_token>
```

服务器会原子地：

1. 清空 `Device.credential_hash`；
2. 增加 `credential_version`；
3. 清除绑定确认和绑定挑战；
4. 撤销该设备全部短期会话；
5. 将账号-设备关系改为 `unbound`；
6. 写入账号和设备都可筛选的业务轨迹。

设备下次带旧凭证请求时会收到 `DEVICE_UNBOUND`。普通 DNS/WiFi/TLS 超时不等于解绑，
固件不得因此擦除凭证。

## 4. 目标首次绑定流程

这是 App 主导、设备证明、服务器归属的流程。用户只看到“登录账号并绑定设备”，
不需要输入绑定码；协议内部仍需要设备用产品绑定密钥证明自己确实在 BLE 链路另一端。

```text
账号登录
  -> App BLE 连接设备
  -> App GET:IDENTITY
  -> App 用账号 Token 请求 binding/challenge
  -> App 把 challenge 转给设备
  -> 设备本地计算 HMAC proof 返回 App
  -> App 用账号 Token 提交 binding/complete
  -> 服务器返回 public_id + device_key + credential_version
  -> App 通过 BLE DEV_BIND_V2 写入设备
  -> 设备持久化凭证并 ACK
  -> 设备下一轮 WiFi 用 auth/devices/session 换短期 Token
```

### 4.1 每一步的责任

1. **账号登录**：App 获取并安全保存账号 Token；不要把账号 Token 写入设备。
2. **读取身份**：App 发送现有 `GET:IDENTITY`，设备返回机器码、是否已配置、公开 ID、
   凭证版本，不返回长期凭证。
3. **领取挑战**：App 调用 `/api/v1/auth/devices/binding/challenge`，请求必须带账号 Token。
4. **设备证明**：App 只转发 `challenge_id` 和 `challenge`；设备使用固件内的
   `PRESCRIPT_DEVICE_BINDING_MASTER_SECRET` 计算 HMAC，不把密钥交给 App。
5. **完成归属**：App 调用 `/api/v1/auth/devices/binding/complete`，转发设备返回的 proof。
   服务器检查挑战属于当前账号、未过期、机器码一致且 proof 正确，然后建立 owner 关系。
6. **写入凭证**：App 收到 `device_key` 后只在内存中保留，写入设备后立即清理字符串；
   设备收到完整且合法的 V2 命令后原子保存。
7. **首次联网确认**：设备调用新 `/api/v1/auth/devices/session` 成功即可确认凭证已经落盘；
   网络响应丢失时，设备保留凭证并在下一轮重试，不重复生成或覆盖。

### 4.2 重试和中断

- challenge 过期：App 重新请求 challenge，不复用旧 proof。
- `complete` 超时：App 不盲目重新注册；可以用同一账号重新获取挑战并重试。
- `DEV_BIND_V2` 写入失败：服务器归属已存在时，App 允许重新读取设备身份并重复完成写入；
  设备在新凭证没有完整落盘前保留旧记录（首次绑定则仍为未配置）。
- App 在写入后崩溃：设备下次联网会用本地凭证认证，账号设备列表可显示绑定状态。
- 设备已被其他账号占用：服务器返回 409，App 只显示已被占用，不尝试覆盖。

## 5. BLE/固件协议：当前已有与必须新增

### 5.1 当前已有，可复用

```text
GET:IDENTITY
```

设备返回：

```text
IDENTITY:{"schema_version":1,"storage_ready":true,
"provisioned":false,"credential_version":0,
"machine_code":"PT-001122AABBCC","public_id":""}
```

当前已有：

```text
DEV_BIND:<public_id>:<device_key>
```

成功 ACK 为现有 `ACK:OK:BOUND:<public_id>`。但它只保存版本 1，不能表达服务器
返回的 `credential_version > 1`，因此不能作为最终的重置/重新绑定协议。

### 5.2 建议新增的 App-设备证明命令

保持现有文本协议和 ACK 习惯，新增一组明确的、不会把秘密写入日志的命令：

```text
// App -> 设备
BIND_PROOF:<challenge_id>:<challenge>

// 设备 -> App
BIND_PROOF_RESULT:<challenge_id>:<verification_code>

// 失败
ACK:ERR:BIND_PROOF:<stable_error_code>
```

约束：

- `challenge_id` 为服务器返回的 UUID；`challenge` 和 proof 使用 URL-safe 无填充字符集。
- 设备只在 BLE 当前连接中处理，不保存 challenge，不联网，不输出产品密钥。
- App 必须按 `challenge_id` 关联响应并设置超时；不能把任意通知当作 proof。
- proof 的规范仍为服务器 v1：

```text
claim_key = HMAC-SHA256(binding_master,
  "PRESCRIPT-CLAIM-KEY-V1\n" + machine_code)
message = "PRESCRIPT-BIND-PROOF-V1\n" + machine_code + "\n"
          + challenge_id + "\n" + challenge
verification_code = base64url_no_padding(HMAC-SHA256(claim_key, message))
```

App 不实现这段 HMAC，也不保存 binding master；固件和服务器保持同一协议版本即可。

### 5.3 建议新增的凭证写入命令

新增版本化命令，保留旧 `DEV_BIND` 只用于历史测试：

```text
DEV_BIND_V2:<public_id>:<credential_version>:<device_key>
```

成功仍返回：

```text
ACK:OK:BOUND:<public_id>
```

失败使用已有 `INVALID_PUBLIC_ID`、`INVALID_DEVICE_KEY`、`IDENTITY_SAVE_FAILED`，
并新增必要的 `DEV_BIND_VERSION_INVALID`。设备必须先校验完整字段，再原子替换
`public_id`、长期凭证和版本；写入失败时不能擦除旧凭证。

## 6. 固件需要完成的接口和改动

### 6.1 网络认证

- `src/net/sources/net_auth_session.cpp`：把会话 URL 从
  `/api/v1/device/session` 改为 `/api/v1/auth/devices/session`。
- `NetAuthSession_Ensure()` 仍发送 `{machine_code, device_key}`，只保留短期 Token 在 RAM。
- HTTP 层需要读取 `X-Device-Error-Code`，把 `DEVICE_UNBOUND`、`DEVICE_DISABLED`、
  `INVALID_DEVICE_CREDENTIALS` 映射为稳定 `NetAuthEnsureResult`，不要解析英文 detail。
- 只有 `DEVICE_UNBOUND` 才调用 `SysDeviceIdentity_ClearBinding()`（需要新增安全清理接口）；
  普通传输失败只重试，`DEVICE_DISABLED` 停止自动重试并提示设备被禁用。

### 6.2 BLE 协议和身份存储

- 在 `sys_protocol`、`sys_router` 中加入 `BIND_PROOF` 请求和
  `BIND_PROOF_RESULT` 响应；HMAC 实现复用 `sys_device_binding.cpp`。
- 加入 `DEV_BIND_V2` 解析、路由和版本字段保存；继续禁止 NFC 注入任何设备绑定命令。
- `GET:IDENTITY` 保持向后兼容，绝不增加 device key 字段。
- `SysDeviceIdentity` 新增“清除当前凭证并保留机器码/公开历史 ID”的明确 API，供
  `DEVICE_UNBOUND` 使用；清除动作必须安全擦除内存缓冲并提交 NVS。
- 首次绑定/账号解绑后，设备 UI 只展示绑定状态，不要求设备自己输入账号或邮箱。

### 6.3 旧自动绑定代码的处理

当前 `net_device_binding.cpp` 会自己请求旧 `/api/v1/device/binding/challenge` 和
`complete`，`app_device_binding.cpp` 也会在设备屏幕上启动这条流程。账号改造完成后，
这条路径应改成“状态显示/等待 App”，或明确保留为受控工厂恢复模式；不能与账号主导绑定
并行争抢设备归属。

## 7. Flutter App 需要完成的接口和改动

### 7.1 Cloud/Auth 层

在 `lib/core/cloud` 新增独立认证边界（例如 `account_api.dart`），不要把 HTTP 调用散落
到 Widget：

```dart
registerRequest(email, password)
registerVerify(challengeId, code)
login(email, password)
requestEmailLogin(email)
verifyEmailLogin(challengeId, code)
loadAccount()
logout()
listDevices()
requestDeviceBindingChallenge(machineCode)
completeDeviceBinding(machineCode, challengeId, verificationCode)
unbindDevice(deviceId)
```

账号 Token 应使用 Android 安全存储；内存、异常、诊断和截图不得出现完整 Token。

把当前 `PrescriptApi.identity()` 的测试 `/submission-context` 链路替换为账号会话：

- `/api/v1/directives` 浏览公开池不需要账号；
- `/api/v1/submissions` 使用账号 Bearer Token，不再发送 `X-Submission-Access`、
  `X-Submission-Context` 或随机测试 ID；
- 投稿内容仍由 `type=common|city` 和 `language=zh` 表达。

### 7.2 BLE 层和 Controller

- `TerminalBleClient` 只负责扫描、连接、发现服务、写入、通知和断开；不直接调用云 API。
- `TerminalProtocol` 增加 `GET:IDENTITY`、`BIND_PROOF`、`BIND_PROOF_RESULT`、
  `DEV_BIND_V2` 的构造和解析，字段必须统一清洗。
- `TerminalController` 编排：连接 -> 读取身份 -> 云端挑战 -> BLE proof -> 云端 complete
  -> BLE 写凭证 -> ACK；页面只订阅状态和结果。
- `BoundDevice` 增加 `machineCode`、`publicId`、`credentialVersion`、`provisioned`、
  `bindingState`，但不保存或暴露 `deviceKey`。
- BLE 写成功不等于业务成功；只有收到匹配的 `BIND_PROOF_RESULT` 或
  `ACK:OK:BOUND` 才推进状态。
- App 断开、超时或页面销毁时取消订阅和计时器；重连不得重复提交旧 challenge。

### 7.3 App 解绑和设备状态

- App 先用账号 Token 调 `DELETE /api/v1/auth/devices/{device_id}`。
- 服务器成功后清理本地账号设备关系；设备自身在下次联网收到 `DEVICE_UNBOUND` 后清除
  长期凭证并回到未绑定状态。
- App 不应通过 BLE 伪造“设备已解绑”，也不应在服务器 DELETE 失败时清除服务端状态假设。

## 8. 后续设备业务请求

设备成功取得短期 Token 后，设备业务统一使用：

```http
Authorization: Bearer <device_access_token>
```

当前保留的设备接口包括：

```http
GET  /api/v1/device/whitelist
PUT  /api/v1/device/whitelist/{target}
DELETE /api/v1/device/whitelist/{target}
POST /api/v1/device/messages
GET  /api/v1/device/mailbox?limit=5
POST /api/v1/device/mailbox/ack
```

这些接口目前仍受全局设备链路开关保护，服务器端要在新认证联调通过后逐项开放。
设备投稿不再是目标能力；账号投稿使用账号 API。所有设备业务日志至少带
`actor_device_id`、关联 `account_id` 和请求来源。

## 9. 实施顺序

1. **先改固件认证入口**：切换 `/auth/devices/session`，增加错误头处理，确保不会误擦凭证。
2. **先做固件 BLE 新命令**：`BIND_PROOF`、`DEV_BIND_V2`，加协议单元测试和串口脱敏检查。
3. **再做 App 账号层**：登录、设备列表、账号绑定状态和安全存储。
4. **实现 App 绑定编排**：BLE 身份 -> challenge -> proof -> complete -> 写入 -> ACK。
5. **用真实手机和真实终端验证**：首次绑定、断电恢复、服务器解绑、禁用、重试和版本升级。
6. **服务器逐项打开设备邮箱/白名单接口**，最后删除或下线旧测试身份和旧自动绑定入口。

## 10. 验收清单

- [ ] 新账号登录后可以通过 BLE 绑定未归属设备。
- [ ] 第二个账号无法覆盖已有 owner；同一账号可绑定多台设备。
- [ ] challenge 过期、App 重启、网络超时不会产生错误归属或清空凭证。
- [ ] `DEV_BIND_V2` 能保存服务器返回的 credential version，重启后仍可认证。
- [ ] 设备使用新 `/auth/devices/session` 成功换取短期 Token，服务器审计同时显示账号和设备。
- [ ] 账号解绑后旧短期 Token 失效，设备收到 `DEVICE_UNBOUND` 后清除长期凭证。
- [ ] 禁用设备收到 `DEVICE_DISABLED`，不会进入无限重试或自动重新绑定。
- [ ] 普通 WiFi/DNS/TLS/HTTP 5xx 失败不会被当成解绑。
- [ ] 设备、App、服务器日志均不出现账号 Token、device key、验证码或 HMAC proof。
- [ ] 真实 Android BLE 通知边界、MTU、重连和长字段已经实机验证；静态检查不能替代实机验证。

## 11. 不要做的事

- 不要让 App 调旧 `/api/v1/device/session`。
- 不要让设备携带邮箱、密码或账号 Token 联网。
- 不要让 App 保存全局 binding master 或自己生成 proof。
- 不要把机器码附加到普通账号投稿、浏览或账号管理请求中。
- 不要把 `device_key` 放进 Flutter `BoundDevice`、SharedPreferences、普通 NVS 配置或日志。
- 不要因为一次网络超时、DNS 失败或服务器 5xx 就删除设备身份。
- 不要在新账号流程稳定前删除历史 migration；但新客户端不能继续依赖旧测试身份。
