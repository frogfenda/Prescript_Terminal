# 指令格式与独立指令库（2026-11-09）

已完成统一内容格式、UUID、统一内部提交，以及普通/特殊/纺织机三类库的独立存储。三类库共享格式与机械读写工具，不共享正文池，也没有总索引文件。外部协议整体重设、手机/Web编辑器、云同步与冲突合并仍待后续。

## 后续待办：统一外部推送协议（明确保留，尚未完成）

- [ ] 设计统一外部推送命令，表达直接内容/随机/特殊来源及投递选项，开放公共属性的完整传递。
- [ ] 迁移当前 `PUSH.TEXT`、`PUSH.SHOW`、`PUSH.NOW`、`SPC.FIRE`。四者现在只是转接到统一内部提交，外部接口尚未统一。
- [ ] 同步迁移独立手机App、WebBLE、NFC、ACK、命令版本声明和协议文档，再决定何时移除旧命令和特殊别名。
- [ ] 区分内容UUID与触发器/链条UUID，明确强制触发从第一步开始的语义，避免把内容推送当作进度重置。

源码保留 `app_push_notify.cpp` 中的 `TODO(推送协议统一)`。特殊/纺织机参数编辑页面、显式状态重置/开关编辑、远端UUID增删改查和文件导出协议后续实现；本次只建立本地整库编辑与独立文件基础。

## 文件布局与所有权

FAT位于外挂NAND，由 `HAL::FatStorage` 管理；设备路径不带VFS挂载前缀 `/fat`。仓库 `fat_data/` 对应FAT根目录，`data/` 对应内部Flash的LittleFS默认镜像。

| 设备路径 | 文件系统 | 所有者与内容 |
| --- | --- | --- |
| `/Resources/prescript/common/text/<zh,en>/prescripts.json` | FAT | `SysOrdinaryPrescripts`：普通指令，所有内容直接参与普通抽取 |
| `/Resources/prescript/special/text/<zh,en>/specials.json` | FAT | `SysSpecials`：独立内容、纯特殊触发器、链条、步骤与概率 |
| `/Resources/loom/text/<zh,en>/oracle.json` | FAT | `SysOracle`：独立内容、分类、分类内相对权重 |
| `/<zh,en>/prescript_state.json` | LittleFS | `SysSpecials`：设备链条进度、完成状态、开关 |

共 **6个随资源提供的库文件＋2个由设备生成的状态文件**。音频路径保持现有目录。`.bak`和临时`.tmp`属于恢复文件，不计入正式8个文件。普通库也依赖FAT可用；FAT不可用时不写库，抽取显示错误/内置提示。LittleFS不再提供默认集中内容库，分区布局无需修改。

启动在FAT挂载和语言确定后由 `main.cpp` 加载普通库；`SysRes_Init`/语言切换加载特殊库和纺织机。普通库为既有列表/跨语言命令保留双语池，特殊/纺织机只持有当前语言。库、字符串、JSON树使用PSRAM优先分配；JSON树解析后释放。

代码边界：`sys_prescript.{h,cpp}` 只负责公共内容格式、UUID、属性准备、库元数据编解码和可恢复JSON读写，不持有全局正文或展示快照。`sys_ordinary_prescripts.{h,cpp}` 只负责普通库。原集中模块 `sys_prescript_catalog`、全局Find、SetReferences、BlockDeletion和跨模块删除保护已移除。当前展示快照由 `SysReminder` 拥有。

## 文件格式和身份

三个库采用schema 2，同样的公共头部和 `contents` 数组：

```json
{
  "schema_version": 2,
  "library_id": "d3146d1c-3d40-4ea0-aa52-9729816cb33d",
  "kind": "special",
  "revision": 1,
  "lang": "ZH",
  "contents": [
    {
      "id": "646a65ff-8ac1-55d5-be69-952c2ca90316",
      "text": "独立指令正文",
      "title": "可选标题",
      "color": "#00FFFF",
      "audio": "default",
      "apply_target": true,
      "alert": false
    }
  ],
  "pure_specials": [],
  "character_chains": []
}
```

`kind` 为 `ordinary`、`special`、`oracle`。普通库没有规则数组；特殊库另含 `pure_specials` 和 `character_chains`；纺织机另含 `entries`。文件只包含本库内容，规则中的 `content_id` 必须在同文件 `contents` 中。特殊指令每一步仍然引用独立内容，正文不内嵌到步骤中；同库多个步骤可复用同一内容。

库UUID表示整份库身份，内容UUID表示独立指令身份，触发器/链条/步骤也各有独立UUID。库类型与归属只存于库头，不在每条指令增加owner字段。默认六个库各自生成并固定库UUID，全部既有内容、触发器、链条、步骤UUID保持不变；本地新内容/迁移库使用硬件随机UUIDv4并立即持久化。复制到另一个库应生成新内容UUID，不建立跨库引用；本次未新增复制入口。

标题/颜色缺省时由 `SysPrescript::Prepare` 补默认标题和青色，显式空标题、黑色、false有效。`audio` 缺省/default使用通用收尾音，支持heads/tails/Ahab/procedure/final，拒绝未知绑定和已移除的none。`apply_target`控制收件人替换，`alert`控制手动警报，整体声音由系统音量控制。

ESP32 ABI断言保持：内容UUID16字节＋颜色2＋音频1＋标志1＝**纯属性20字节**；标题/正文描述符各8，完整固定结构36字节，动态文字另计。普通库不再重复保存每条16字节的候选ID数组。库UUID/版本为每库开销，不增加单条内容结构。

## 编辑、删除和进度

普通库按UUID增删改，保存失败回滚；每条内容直接是候选，因此删除就是删除该普通指令，不再区分候选移除/全局内容删除，也不检查其它库的健康状态。

`SysOrdinaryPrescripts::ReplaceLibrary`、`SysSpecials::replaceLibrary` 和 `SysOracle::replaceLibrary` 接受完整文件，校验同库UUID、expected_revision以及文档revision=expected+1；再校验全部正文、本库引用和业务规则。正文和规则一次同文件提交，保存成功后才发布新内存库。不同库UUID返回LIBRARY_CONFLICT；USB手动整文件替换在下次启动/该资源重载时生效，不做自动合并或实时文件监视。

特殊触发器保存 `probability_bp`；链条保存 `base_bp`、`increment_bp`、`max_bp` 和按顺序的steps。概率以万分位表示，10000=100%；所有链条可达最大概率与纯特殊概率之和不能超过10000。保持链条优先、纯特殊随后、普通兜底，普通和特殊是抽取流程合作，并非共享存储。

链条状态仍按链条UUID＋下一步骤UUID存于原LittleFS路径，schema 1保持不变。重排步骤继续执行原待执行UUID，概率增量按新顺序位置计算；删除待执行步骤返回STATE_CONFLICT，不能静默重置。新链条先保存初始状态再提交库，失败最多留下未使用状态；已移除链条的状态暂保留。更新资源不附带设备进度，分享文件不会分享个人进度。

纺织机entries保存 `content_id/type/weight`，同分类同内容重复引用拒绝，权重必须为正整数，各分类总和不超过INT32_MAX。每语言/分类的上次UUID只保存在RAM，用于连续去重，单候选允许重复。

## 统一内部提交和外部兼容

唯一提交入口 `SysReminder_Submit(request, options)` 保持。Direct同步借用完整属性并复制快照，不写库；Random交给特殊抽取流程；Special接受触发器/链条UUID和兼容别名。Queue排队、Immediate立即展示、CurrentPage仅交付当前页面；页面返回策略、计时去重标识、邮件确认标识独立保存。

当前展示通过 `SysReminder_Presented()` 读取，队列和已展示内容均拥有文字；各库编辑、扩容或语言切换不会替换已有快照。队列满/文字复制失败/状态保存失败不消费链条进度，状态提交后只移动快照。取消已接受提醒不倒退进度；RAM队列断电会丢未展示消息，不提供端到端恰好一次投递。纺织机继续自己的页面与快照，复用公共Prepare，不强制进提醒队列。

旧协议仍保留：PUSH.TEXT默认属性；SHOW支持旧标题/颜色且空标题表示默认；NOW随机排队；SPC.FIRE立即中断且链条从第一步开始，未知ID返回WARN。PRE.ADD/DEL仍是语言＋正文、删除首条同文，LIST仍为 `{t}`；这些命令已转接普通库。SPC.TEXT预览不推进，legacy_id仅用于旧客户端兼容。未改变手机/Web协议。

## 升级、迁移与恢复

新版完整库是权威数据，包括空库。普通库目标文件及bak都不存在时，优先从旧LittleFS集中 `/<lang>/prescripts.json` 按ordinary集合提取，保留内容UUID；集中库不存在才导入原语言目录TXT，最后/assets TXT兜底。同文TXT生成不同UUID，成功写FAT后重启沿用。

特殊/纺织机原路径仍是上一版schema 1 UUID引用规则时，从旧集中库提取规则实际引用的内容，组合完整schema 2文件；完整校验通过、保存成功才使用新库。只迁移上一版UUID格式，无版本的更早JSON仍拒绝。旧集中JSON/TXT原件不删除，迁移失败保留原规则，可在故障解除后重试；新版schema 2文件不读取旧集中库。

已有设备建议：先备份并保留旧LittleFS集中JSON、旧FAT UUID规则和状态，再更新固件，让首次启动迁移自定义数据。**在迁移之前复制默认普通库到FAT会使其成为权威库，旧自定义普通库不会再自动导入。** 新设备或明确要替换为默认库时，直接复制六份配套资源；导出/替换特殊库无需复制状态文件。不要为了本次拆分重刷默认LittleFS覆盖个人配置/进度。

写入继续采用tmp→长度检查→回读解析→轮换bak→重命名正式文件。启动只读正式/bak，语法有效但schema、引用、进度不合法时保留现场并停用该库，不以旧备份悄悄回滚语义错误。无效特殊/纺织机不会阻止普通库编辑。FAT物理掉电一致性和USB实际文件操作仍需真机验证，宿主故障注入不替代硬件测试。

## 验证

```powershell
python tools/prescript/validate_catalog.py
python tests/prescript/run_catalog.py
python tests/protocol/run_protocol.py
python tests/clocks/run_clock_services.py
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3 -t buildfs
```

资源校验覆盖六份库元数据、UUID、公共属性、本库引用、概率和权重。宿主联测覆盖集中库/TXT迁移、原UUID/进度延续、迁移保存失败重试、旧源保留、新版权威、普通/特殊/纺织机故障隔离、跨库引用拒绝、整库保存失败不发布、库身份/版本冲突、待执行步骤删除冲突，以及统一推送的属性、队列、邮箱确认、旧命令兼容。未自动烧录。
