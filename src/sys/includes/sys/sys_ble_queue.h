/*
【模块职责】命令行队列接口。蓝牙、NFC、串口三种来源把“完整的一行请求”放进这里，主循环逐行取出交给命令框架。
【重要约束】入队可在任意任务调用（蓝牙回调在 Core 0，NFC 在自己的任务）；出队只在主循环调用。
队列满或整行超长时不会丢掉已有内容，而是记一条“拒收通知”，由主循环据此回 BUSY / TOO_LONG。
*/
#pragma once
#include <Arduino.h>
#include "sys/sys_constants.h"

// 请求来源。数值是位掩码，命令组用它声明允许哪些来源调用。
enum class SysCmdSource : uint8_t
{
    Ble = 1 << 0,
    Nfc = 1 << 1,
    Serial = 1 << 2,
};

// 一条待执行的请求行；text 已去掉结尾的 \r\n 并以 \0 结尾。
struct SysCmdLine
{
    SysCmdSource source;
    uint32_t session; // BLE 的连接代次；NFC/串口为 0，回包不得跨代次发送。
    uint16_t length;
    char text[PrescriptConst::CMD_LINE_MAX];
};

// 拒收原因：队列满或单行超长。head 保留请求行开头，主循环从中取出“组.动词#编号”回错误。
enum class SysCmdReject : uint8_t
{
    Busy,
    TooLong,
};

struct SysCmdRejectNotice
{
    SysCmdSource source;
    uint32_t session;
    SysCmdReject reason;
    char head[40];
};

/**
 * 【接口说明】把一行请求放入队列。text 不需要以 \0 结尾，length 不含换行。
 * 队列满时返回 false，并自动记录一条 Busy 拒收通知；BLE 调用方必须传入当前连接代次 session。
 */
bool SysCmdQueue_Push(SysCmdSource source, const char *text, size_t length, uint32_t session = 0);

/** 【接口说明】记录一条拒收通知（例如行组装发现超长）；只保留请求开头用于回包。 */
void SysCmdQueue_Reject(SysCmdSource source, SysCmdReject reason, const char *head, size_t length, uint32_t session = 0);

/** 【接口说明】主循环取出下一条请求行；没有时返回 false。 */
bool SysCmdQueue_Pop(SysCmdLine &out);

/** 【接口说明】主循环取出下一条拒收通知；没有时返回 false。 */
bool SysCmdQueue_PopReject(SysCmdRejectNotice &out);

/** 【接口说明】清空某个来源尚未执行的请求和拒收通知，例如蓝牙断开时丢弃旧连接留下的命令。 */
void SysCmdQueue_DropSource(SysCmdSource source);

/**
 * 按行组装器：把任意切分的字节流拼成以换行结尾的完整行并入队。
 * 每个来源各用一个实例，只在该来源的单一任务里调用 feed()。
 */
class SysCmdLineAssembler
{
public:
    explicit SysCmdLineAssembler(SysCmdSource source) : source_(source) {}

    /** 输入一段字节；遇到 \n 时把这一行入队，\r 一律忽略。超长行整行丢弃并记 TooLong；BLE 调用方传入当前连接代次 session。 */
    void feed(const uint8_t *data, size_t length, uint32_t session = 0);

    /** 丢弃半行数据，例如连接断开时。 */
    void reset();

private:
    SysCmdSource source_;
    char buffer_[PrescriptConst::CMD_LINE_MAX];
    size_t used_ = 0;
    bool overflow_ = false;
};
