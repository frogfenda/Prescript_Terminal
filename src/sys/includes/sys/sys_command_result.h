/*
【模块职责】业务操作结果。每个应用的业务入口（本机界面和蓝牙命令共用）都返回这个结构，
界面据此决定提示和音效，蓝牙处理函数把它原样翻译成 ACK 回包。
【重要约束】不再有全局“当前命令结果”上下文；结果只通过返回值传递。
*/
#pragma once
#include <Arduino.h>

enum class SysCmdResultLevel : uint8_t
{
    Ok,
    Warn,
    Error
};

struct SysOpResult
{
    SysCmdResultLevel level = SysCmdResultLevel::Ok;
    const char *code = "OK";  // 大写下划线业务码，必须指向静态字符串
    String detail;            // 可选细节，例如被操作条目的名称

    bool ok() const { return level != SysCmdResultLevel::Error; }

    static SysOpResult Make(SysCmdResultLevel level, const char *code, const String &detail)
    {
        SysOpResult result;
        result.level = level;
        result.code = code;
        result.detail = detail;
        return result;
    }
    static SysOpResult Ok(const char *code, const String &detail = String())
    {
        return Make(SysCmdResultLevel::Ok, code, detail);
    }
    static SysOpResult Warn(const char *code, const String &detail = String())
    {
        return Make(SysCmdResultLevel::Warn, code, detail);
    }
    static SysOpResult Error(const char *code, const String &detail = String())
    {
        return Make(SysCmdResultLevel::Error, code, detail);
    }
};
