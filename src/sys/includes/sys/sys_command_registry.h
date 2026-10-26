/*
【模块职责】蓝牙指令框架（协议 v2）。按“组码.动词”查找命令，按参数规格校验字段，调用所属模块的处理函数，
并负责 ACK / DAT / EVT 回包编码、有界列表快照和逐条发送。
【使用方式】每个模块在自己文件末尾的【蓝牙命令区】定义一个 SysCmdGroup，并在已有的初始化入口里调用
SysCommand_RegisterGroup 注册；处理函数只做“参数 → 业务入口 → 回包”的翻译，不写业务规则。
【重要约束】处理函数和列表回调都在 Arduino 主循环同步执行；协议细节见 docs/ble-protocol-v2.md。
*/
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "sys/sys_ble_queue.h"
#include "sys/sys_command_result.h"
#include "sys/sys_constants.h"

// ---------- 参数规格 ----------

// Int：十进制整数（可带负号）；Str：取到下一个冒号为止；Rest：剩余全部内容，只能是最后一个参数；
// Choice：固定取值之一，choices 写成 "ZH|EN"。
enum class SysCmdArgType : uint8_t
{
    Int,
    Str,
    Rest,
    Choice
};

enum : uint8_t
{
    ARG_TRIM = 1 << 0,      // 校验前去掉首尾空白
    ARG_OPTIONAL = 1 << 1,  // 可缺省；只用于最后一个参数，空字段也视为缺省
};

struct SysCmdArgSpec
{
    const char *name;     // 参数名，出现在 ARG_* 错误细节里
    SysCmdArgType type;
    int32_t min;          // Int：最小值；Str/Rest：最小字节数
    int32_t max;          // Int：最大值；Str/Rest：最大字节数，0 表示不限
    uint8_t flags;
    const char *choices;  // 仅 Choice 使用
};

#define SYS_ARG_INT(name, lo, hi) {name, SysCmdArgType::Int, lo, hi, 0, nullptr}
#define SYS_ARG_INT_OPT(name, lo, hi) {name, SysCmdArgType::Int, lo, hi, ARG_OPTIONAL, nullptr}
#define SYS_ARG_STR(name, lo, hi, flags) {name, SysCmdArgType::Str, lo, hi, flags, nullptr}
#define SYS_ARG_REST(name, lo, hi, flags) {name, SysCmdArgType::Rest, lo, hi, flags, nullptr}
#define SYS_ARG_CHOICE(name, list) {name, SysCmdArgType::Choice, 0, 0, 0, list}
#define SYS_COUNT_OF(array) static_cast<uint8_t>(sizeof(array) / sizeof((array)[0]))

// ---------- 命令与组 ----------

enum : uint16_t
{
    CMD_LANG_LOCKED = 1 << 0,  // 第 0 个参数是 ZH|EN 语言，受编译期语言锁限制
    CMD_SYNC = 1 << 1,         // 本命令是组的列表流，参与 SYS.SYNC 全量同步
};

enum : uint8_t
{
    SRC_BLE = static_cast<uint8_t>(SysCmdSource::Ble),
    SRC_NFC = static_cast<uint8_t>(SysCmdSource::Nfc),
    SRC_SERIAL = static_cast<uint8_t>(SysCmdSource::Serial),
    SRC_ALL = 0xFF,
};

/** 已校验的参数。字段已反转义、按需去空白，整数和选项已解析；只在处理函数调用期间有效。 */
class SysCmdArgs
{
public:
    uint8_t count() const { return count_; }
    /** 第 i 个参数是否提供（可选参数缺省或为空时为 false）。 */
    bool has(uint8_t i) const { return i < count_ && (present_ & (1u << i)) != 0; }
    /** 字段文本；缺省时返回空串。 */
    const char *str(uint8_t i) const { return has(i) ? fields_[i] : ""; }
    String string(uint8_t i) const { return String(str(i)); }
    size_t len(uint8_t i) const { return has(i) ? lengths_[i] : 0; }
    /** Int 参数的值；缺省时返回 0。 */
    int32_t num(uint8_t i) const { return has(i) ? numbers_[i] : 0; }
    /** Choice 参数在 choices 中的序号；缺省时返回 -1。 */
    int choice(uint8_t i) const { return has(i) ? static_cast<int>(numbers_[i]) : -1; }

private:
    friend struct SysCommandFramework;
    char buffer_[PrescriptConst::CMD_LINE_MAX];
    const char *fields_[PrescriptConst::MAX_CMD_ARGS];
    uint16_t lengths_[PrescriptConst::MAX_CMD_ARGS];
    int32_t numbers_[PrescriptConst::MAX_CMD_ARGS];
    uint16_t present_ = 0;
    uint8_t count_ = 0;
};

/** 一次请求的回包上下文。每个请求最终恰好一条 ACK；处理函数没有调用 ack 时框架补发 OK:DONE。 */
class SysCmdCtx
{
public:
    SysCmdSource source() const { return source_; }
    /** 发送最终回执；同一请求重复调用时只有第一次生效。 */
    void ack(const SysOpResult &result);
    void ok(const char *code, const String &detail = String()) { ack(SysOpResult::Ok(code, detail)); }
    void error(const char *code, const String &detail = String()) { ack(SysOpResult::Error(code, detail)); }
    /** 把 DAT 编码进本请求快照；拥塞由框架重试。超限/内存失败返回 false，最终 ACK 自动改为 ERR。 */
    bool data(const JsonDocument &document);
    bool acked() const { return acked_; }

private:
    friend struct SysCommandFramework;
    SysCmdSource source_ = SysCmdSource::Ble;
    char id_[9] = {};
    char name_[28] = {};
    bool acked_ = false;
};

using SysCmdHandler = void (*)(SysCmdCtx &ctx, const SysCmdArgs &args);
/** 列表快照回调：同一轮主循环从 index=0 连续调用；只读内存、不做 I/O。填好 out 返回 true，结束返回 false。 */
using SysCmdStream = bool (*)(uint16_t index, JsonDocument &out);

struct SysCmdSpec
{
    const char *verb;
    const SysCmdArgSpec *args;
    uint8_t arg_count;
    uint16_t flags;
    SysCmdHandler handler;  // 普通命令
    SysCmdStream stream;    // 列表命令；与 handler 二选一
};

struct SysCmdGroup
{
    const char *code;       // 组码，同时是 SYS.SYNC 的范围名
    uint8_t version;        // 组内命令有增改时加一，SYS.INFO 对外公布
    uint8_t sources;        // 允许的请求来源（SRC_* 位掩码）
    const SysCmdSpec *cmds;
    uint8_t cmd_count;
};

// ---------- 框架入口 ----------

/** 【接口说明】启动时调用一次：注册框架自带的 SYS 组。 */
void SysCommand_Init();

/** 【接口说明】注册一个命令组；组码重复、超过上限或结构不合法时拒绝并打印中文日志。 */
bool SysCommand_RegisterGroup(const SysCmdGroup *group);

/**
 * 【接口说明】主循环每轮调用一次：读取串口输入与当前回包；当前 ACK 进入来源后，
 * 才消费下一条拒收通知或请求。列表在执行时生成有界快照，发送按帧推进。
 */
void SysCommand_Update();

/**
 * 【接口说明】取出同步闪框下一帧的强度；命令框架只维护时序，不访问屏幕。
 * 【返回】当前未到绘制时间或闪框已结束时返回 false；成功时写入 0~255 的整帧强度。
 */
bool SysCommand_TakeSyncFlash(uint8_t *intensity);

/** 【接口说明】向已连接的客户端推送一条事件：EVT:<name>:<JSON>。只发蓝牙，不排队重试。 */
void SysCommand_Event(const char *name, const JsonDocument &payload);

/** 【接口说明】通知客户端某个持久数据组已变化；客户端应随后发 SYS.SYNC:<组> 取新快照。 */
void SysCommand_NotifyChanged(const char *group);

/**
 * 【接口说明】在一段文本里找第一个“已注册组码 + .”出现的位置，NFC 用它跳过卡片里的 NDEF 头部字节。
 * 找不到返回 -1。只读访问组表，可在 NFC 任务中调用。
 */
int SysCommand_FindLineStart(const char *text, size_t length);
