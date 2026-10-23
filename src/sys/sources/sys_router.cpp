/*
【模块职责】系统命令入口。BLE/NFC 文本到达主循环后只经过解析和统一命令注册表，
注册表直接调用领域 Handler；本文件负责来源入口、语言权限和 ACK 编码。
*/
#include "sys/sys_router.h"
#include "sys/sys_protocol.h"
#include "sys/sys_command_registry.h"
#include "sys/sys_command_result.h"
#include "sys/sys_ble.h"
#include "sys/sys_event.h"
#include "lang/terminal_lang.h"
#include <Arduino.h>

static void Router_SendAckResult(SysCommandType type, SysCmdResultLevel level,
                                 const String &code, const String &detail = String())
{
    if (!SysCommandRegistry_IsMutating(type) && level != SysCmdResultLevel::Error)
        return;
    String out = "ACK:";
    switch (level)
    {
    case SysCmdResultLevel::Ok: out += "OK:"; break;
    case SysCmdResultLevel::Warn: out += "WARN:"; break;
    case SysCmdResultLevel::Error: out += "ERR:"; break;
    case SysCmdResultLevel::None: default: out += "OK:"; break;
    }
    out += SysCommandRegistry_CommandName(type);
    out += ":";
    out += code.length() > 0 ? code : "DISPATCHED";
    if (detail.length() > 0) { out += ":"; out += detail; }
    SysBLE_Notify(out.c_str());
}

static void Router_SendAckErr(SysCommandType type, const String &reason)
{
    Router_SendAckResult(type, SysCmdResultLevel::Error, reason);
}

// 【函数说明】执行一条完整命令：解析、权限检查、直接 Handler、独立结果和 ACK。
static void SysRouter_ExecuteSingle(const String &raw_cmd)
{
    SysParsedCommand command = SysProtocol_ParseSingle(raw_cmd);
    if (command.type == SysCommandType::Invalid)
    {
        Serial.printf("[协议层] 收到无效命令，长度=%u，原因=%s。\n", static_cast<unsigned>(raw_cmd.length()), command.error.c_str());
        Router_SendAckErr(command.type, command.error);
        return;
    }
    if (command.type == SysCommandType::Unknown)
    {
        Serial.printf("[协议层] 收到未知命令，长度=%u。\n", static_cast<unsigned>(raw_cmd.length()));
        Router_SendAckErr(command.type, "UNKNOWN_COMMAND");
        return;
    }
    if ((command.type == SysCommandType::PrescriptAdd || command.type == SysCommandType::PrescriptDel) &&
        !TerminalLang::Accepts((SystemLang_t)command.lang))
    {
        Serial.printf("[协议层] 语言锁定：拒绝 %s 命令写入 %s 库，当前固件=%s\n",
                      SysCommandRegistry_CommandName(command.type), TerminalLang::Code((SystemLang_t)command.lang), TerminalLang::BUILD_CODE);
        Router_SendAckResult(command.type, SysCmdResultLevel::Error, "LANG_LOCKED", TerminalLang::BUILD_CODE);
        return;
    }

    SysCmdResult result;
    const bool mutating = SysCommandRegistry_IsMutating(command.type);
    if (mutating) SysCmdResult_Begin(result);
    const bool dispatched = SysCommandRegistry_Dispatch(command);
    if (!dispatched)
    {
        if (mutating) SysCmdResult_End();
        Router_SendAckErr(command.type, "DISPATCH_FAILED");
        return;
    }
    if (mutating)
    {
        Router_SendAckResult(command.type,
                             result.level == SysCmdResultLevel::None ? SysCmdResultLevel::Ok : result.level,
                             result.code.length() > 0 ? result.code : "DISPATCHED", result.detail);
        SysCmdResult_End();
    }
}

// 【函数说明】处理来自 BLE/Web/NFC 的宏文本；换行和 | 仍保持原协议兼容。
void SysRouter_ProcessBLE(const String &msg)
{
    int cursor = 0;
    String command;
    while (SysProtocol_NextMacroCommand(msg, cursor, command))
    {
        Serial.printf("[宏引擎] 已提取子指令，长度=%u。\n", static_cast<unsigned>(command.length()));
        SysRouter_ExecuteSingle(command);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 【函数说明】NFC 扫描仍通过事件解耦硬件服务，但命令正文复用同一直接执行入口。
void _Cb_NfcScanned(void *payload)
{
    Evt_NfcScanned_t *packet = static_cast<Evt_NfcScanned_t *>(payload);
    if (!packet) return;
    String text = String(packet->payload);
    if (text.indexOf("DEV_BIND:") >= 0 || text.indexOf("DEV_BIND_V2:") >= 0 || text.indexOf("BIND_PROOF:") >= 0)
    {
        Serial.println("[路由中心] 已拒绝 NFC 中的设备身份绑定命令。");
        return;
    }
    Serial.printf("[路由中心] 收到 NFC 物理卡片指令，长度=%u。\n", static_cast<unsigned>(text.length()));
    SysRouter_ProcessBLE(text);
}

void SysRouter_Init()
{
    SysEvent_Subscribe(EVT_NFC_SCANNED, _Cb_NfcScanned);
}
