# 指令内容库基础重构（2026-11-06）

本次落地独立内容、稳定身份、引用规则、模块状态和展示快照。外部 API 的整体重新设计、手机/Web 编辑器、云端同步/冲突合并不在本次范围。

## 文件与所有权

| 文件 | 文件系统 | 负责内容 |
| --- | --- | --- |
| `/<zh,en>/prescripts.json` | LittleFS | 该语言全部独立指令、普通候选 ID、外部引用保护、schema/revision |
| `/Resources/prescript/special/text/<zh,en>/specials.json` | FATFS | 纯特殊触发器、链条公共参数、按顺序排列的步骤引用 |
| `/Resources/loom/text/<zh,en>/oracle.json` | FATFS | 分类、内容 ID、分类内相对权重 |
| `/<zh,en>/prescript_state.json` | LittleFS | 链条 ID、下一步骤 ID、完成状态、开关 |

分区表保持不变。全部正文现在集中在 LittleFS 内容库，FATFS 规则不再重复存正文。
一个内容可以被多个模块/链条引用，没有强制单一 owner。普通候选是显式 ID 集合，未加入普通候选的特殊/纺织机内容不会被普通随机误抽。

## 身份与公共属性

运行时 ID 为 16 字节，JSON 为标准 UUID 字符串。仓库默认资源已生成固定 UUIDv5，历史生成命名空间为 `5b9f6689-324d-48e6-9de5-0c242743cda7`；这些身份直接保存在资源中，运行时不再生成 UUIDv5。固件新增内容、旧设备本地 TXT 迁移用硬件随机 UUIDv4；迁移成功后持久化，编辑/重启不再生成。相同正文保留不同 ID；本地 TXT 不按行号跨设备共用身份。

内容格式示例（ID 仅示意）：

```json
{
  "id": "646a65ff-8ac1-55d5-be69-952c2ca90316",
  "text": "独立指令正文",
  "title": "可选标题",
  "color": "#00FFFF",
  "audio": "default",
  "apply_target": true,
  "alert": false
}
```

标题和颜色缺省时在快照中补当前语言默认标题、青色；显式空标题/黑色有效。音频 `default`（或缺省）使用通用收尾音，`none` 只静默收尾；解码过程音保留原行为。现支持稳定绑定 heads/tails/Ahab/procedure/final，未知绑定拒绝新格式。`apply_target` 控制本地收件人替换，`alert` 控制手动抽取先弹警报，两者独立。

内存 ABI 用 ESP32 编译期断言验证：

| 字段 | 字节 |
| --- | ---: |
| UUID | 16 |
| RGB565 | 2 |
| 音频枚举 | 1 |
| 标志位 | 1 |
| 纯属性合计 | **20** |
| 标题、正文所有权描述符各 8 字节 | 16 |
| 完整固定结构（不含动态文字） | **36** |

普通候选每条另存 16 字节 ID；特殊/纺织机引用另计。字符串与容器优先 PSRAM，JSON DOM 解析/写入后释放；容量余量、堆分配元数据和编辑时临时副本不包含在上述 36 字节内。

## 特殊与纺织机规则

纯特殊触发器有独立触发器 UUID、内容 ID、`probability_bp`。
链条有独立链条 UUID、名称、`base_bp`、`increment_bp`、`max_bp`、steps。
每一步有独立步骤 UUID 和内容 ID；同一内容可以重复出现在不同步骤。基础/递增/上限都以万分位为单位，10000=100%；所有可达最大概率之和不得超过 10000。保留链条优先、纯特殊随后、普通兜底的顺序，不做相对权重归一化。

状态按 chain_id + next_step_id 保存，完成状态单列。步骤重排不改变“下一步是谁”，概率增量仍按该步骤在新顺序中的位置计算。删除当前待执行步骤返回 STATE_CONFLICT，不静默重置。规则整体替换支持新增链条、名称/概率参数/步骤/纯特殊触发器修改；先保存新链条状态，再发布规则，失败最多留下未使用状态。已移除链条的状态暂保留，避免误删后丢进度。

`SysSpecials::replaceRules` / `SysOracle::replaceRules` 是本地主循环的整体编辑基础，需 expected_revision 和文档 revision=expected+1；全部校验后提交，失败不发布半份规则。编辑 UI、状态重置/开关编辑 API、导出协议后续再做。

纺织机规则为 entries（content_id/type/weight）。同分类同内容重复引用拒绝，正整数权重按分类相对抽取，总和不超过 INT32_MAX。运行期间按每个语言/分类的上次内容 UUID 避免连续重复，单候选允许重复；这项短期去重状态不写 Flash，重启重新开始。

## 编辑、删除和展示

- 内容更新按 ID 保存，普通候选与正文在同一 JSON 提交；删除失败回滚内存。
- `Delete(lang,id)` 永久删除内容前由规则模块校验对应语言的磁盘引用，同时保护当前仍加载的规则。被引用返回 IN_USE；FAT不可用、旧格式规则或版本损坏时返回 REFERENCES_UNAVAILABLE，防止误删。
- `RemoveOrdinary(lang,id)` 只移出普通候选，不删除内容；永久删除和移除引用是两项操作。
- 档案页确认删除锁定当时展示的 UUID，远端删除造成下标移动时不会删错另一条。
- 展示快照拥有完整标题/正文及全部属性。内容库编辑、vector 扩容或语言重载不会改变已排队内容。
- 随机推送在入队时选内容，容量与文字复制成功后才持久化链条进度，随后无分配移动入队。队列满/复制失败/进度保存失败不消费进度。取消已接受的提醒不会倒退链条；队列仍是 RAM 队列，断电会丢未展示提醒，不提供端到端恰好一次投递。
- 展示所有者为 SysPrescriptCatalog，不再借用 sysSpecials 的可变全局结果。纺织机仍保留自己的页面流程与快照。

## 迁移和恢复

新内容 JSON 不存在时才导入旧 TXT（旧语言目录优先，/assets 为兜底）。TXT 原件保留。有效的新空候选库是权威数据，不重新导入旧 TXT。仓库 TXT 暂留为旧格式示例/迁移输入；新固件编辑只写 JSON，今后维护默认指令应编辑 JSON。

旧 specials/oracle JSON 转换和旧配置进度迁移已移除。规则只接受新格式，旧文件保留现场但不会自动导入；部署时应同时提供配套内容与规则。链条缺少 UUID 状态时默认启用、从第一步开始；已有 UUID 状态继续恢复，不再读取或写入旧配置进度/开关。特殊规则保留 `legacy_id` 仅服务于现有 SPC 协议；链条旧下标和纺织机旧 ID 已删除。

每次写入：tmp → 检查写入长度 → 回读解析 → 正式文件轮换 bak → tmp 重命名正式文件。启动优先正式文件，语法解析失败或正式文件不存在时尝试 bak；遗留 tmp 不视为提交成功。正式 JSON 语法有效但 schema/引用/状态不合法时保留现场、停用相应模块，**不自动用旧备份覆盖语义损坏或未知版本**。这不是跨文件事务：内容和引用先保存，规则随后提交，失败可能留下未引用内容/保守的引用保护，但不发布悬空规则。FAT 物理掉电一致性仍需实机验证。

升级必须保持内容 JSON 与新引用规则配套。只替换为新规则而没有对应内容，模块会拒绝加载；已有自定义 TXT 的设备应优先让固件迁移旧文件，避免刷默认 LittleFS 覆盖用户数据。

## 兼容性和验证

PRE.ADD/DEL 原来的语言+正文和首条同文删除语义保持，LIST 仍返回 `{t}`；UUID 删除入口在固件内容库中，新的远端 UUID 编辑协议尚未添加。SPC 返回旧 legacy_id 兼容客户端，内部使用 UUID，同时支持 36 字符 UUID 查询/强制触发。SPC.TEXT 预览下一步不推进；SPC.FIRE 保留立即打断、链条从第一条重启的旧语义。PUSH.TEXT/SHOW/NOW 的字段形状保持，写入错误准确返回 OUT_OF_MEMORY / SAVE_FAILED / QUEUE_FULL。

验证命令：

```powershell
python tools/prescript/validate_catalog.py
python tests/prescript/run_catalog.py
python tests/protocol/run_protocol.py
python tests/clocks/run_clock_services.py
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3
```

新增宿主测试编译真实模块，覆盖 UUIDv4 生成/UUID 解析、重复正文精确删除、RGB565全部65536颜色往返、引用保护、快照内存失败、旧TXT导入、旧规则JSON拒绝且原件不变、新链条初始状态、重排后按ID恢复、版本冲突、短写/重命名失败/备份恢复、队列满及保存失败不推进。真机显示/存储掉电/休眠恢复未替代为宿主验证。
