/*
【模块职责】业务命令回执实现。用一个全局捕获槽记录本次命令的结果级别、代码和详情；Take 后自动关闭捕获，防止旧结果影响下一条命令。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_command_result.cpp
#include "sys/sys_command_result.h"

static SysCmdResult g_last_result;
static bool g_has_result = false;
static bool g_capture_active = false;

static void SetResult(SysCmdResultLevel level, const char *code, const String &detail)
{
    // 只有 Router 主动开启捕获时才记录结果。
    // 这样 UI 本地操作、网络后台 API 注入不会污染下一条 BLE 命令的 ACK。
    if (!g_capture_active)
        return;

    g_last_result.level = level;
    g_last_result.code = code ? code : "UNKNOWN";
    g_last_result.detail = detail;
    g_has_result = true;
}

void SysCmdResult_Begin()
{
    g_last_result = SysCmdResult();
    g_has_result = false;
    g_capture_active = true;
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

bool SysCmdResult_Take(SysCmdResult &out)
{
    g_capture_active = false;
    if (!g_has_result)
        return false;
    out = g_last_result;
    g_has_result = false;
    return true;
}

void SysCmdResult_Cancel()
{
    g_capture_active = false;
    g_has_result = false;
    g_last_result = SysCmdResult();
}
