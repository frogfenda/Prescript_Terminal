/*
【模块职责】业务命令回执实现。只保存当前调用栈绑定的结果对象指针；命令完成后由调用方直接读取结果。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_command_result.cpp
#include "sys/sys_command_result.h"

static SysCmdResult *g_current_result = nullptr;

static void SetResult(SysCmdResultLevel level, const char *code, const String &detail)
{
    // 只有命令 Router 绑定了结果对象时才记录结果；本地 UI 操作不会产生 BLE ACK。
    if (g_current_result == nullptr)
        return;

    g_current_result->level = level;
    g_current_result->code = code ? code : "UNKNOWN";
    g_current_result->detail = detail;
}

void SysCmdResult_Begin(SysCmdResult &out)
{
    out = SysCmdResult();
    g_current_result = &out;
}

void SysCmdResult_Ok(const char *code, const String &detail)
{
    SetResult(SysCmdResultLevel::Ok, code, detail);
}

void SysCmdResult_Warn(const char *code, const String &detail)
{
    SetResult(SysCmdResultLevel::Warn, code, detail);
}

void SysCmdResult_Error(const char *code, const String &detail)
{
    SetResult(SysCmdResultLevel::Error, code, detail);
}

void SysCmdResult_End()
{
    g_current_result = nullptr;
}
