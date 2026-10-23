/*
【模块职责】业务命令回执接口。Router 调用直接 Handler 前绑定栈上结果对象，业务入口写入 OK/WARN/ERR，Router 随后转成 ACK。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_command_result.h
#pragma once
#include <Arduino.h>

// Stage 5B: 业务级命令结果回执。
// Router 为每次命令调用创建栈上结果对象；业务函数只写入当前调用上下文，
// 命令结束后由 Router 直接读取该对象。不同来源不会共享一个全局回执槽。

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

// 【接口说明】绑定当前命令的结果对象。绑定只在当前主循环调用栈内有效。
void SysCmdResult_Begin(SysCmdResult &out);
void SysCmdResult_Ok(const char *code, const String &detail = String());
// 【接口说明】业务模块报告非致命结果，例如 EXISTS、NOT_FOUND、RECYCLED_EXPIRED。
void SysCmdResult_Warn(const char *code, const String &detail = String());
void SysCmdResult_Error(const char *code, const String &detail = String());
// 【接口说明】解除当前命令结果上下文；没有上下文时业务函数不会产生回执。
void SysCmdResult_End();
