/*
【模块职责】业务命令回执接口。Router 在发布会改变状态的事件前 Begin，业务回调处理完成后写入 OK/WARN/ERR，Router 取走后转成 ACK。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_command_result.h
#pragma once
#include <Arduino.h>

// Stage 5B: 业务级命令结果回执。
// Router 在发布可变更系统状态的事件前 Begin；同步事件回调处理完成后调用 Ok/Warn/Error。
// Router 再统一把结果转换成 ACK:OK/WARN/ERR 回传给 WebBLE。

enum class SysCmdResultLevel : uint8_t
{
    None = 0,
    Ok,
    Warn,
    Error
};

struct SysCmdResult
{
    SysCmdResultLevel level = SysCmdResultLevel::None;
    String code;
    String detail;
};

// 【接口说明】开始捕获本条命令的业务结果，Router 在发布可变更状态的事件前调用。
void SysCmdResult_Begin();
void SysCmdResult_Ok(const char *code, const String &detail = String());
// 【接口说明】业务模块报告非致命结果，例如 EXISTS、NOT_FOUND、RECYCLED_EXPIRED。
void SysCmdResult_Warn(const char *code, const String &detail = String());
void SysCmdResult_Error(const char *code, const String &detail = String());
// 【接口说明】Router 取走本次捕获结果；成功取走后清空捕获状态。
bool SysCmdResult_Take(SysCmdResult &out);
void SysCmdResult_Cancel();
