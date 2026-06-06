/*
【模块职责】系统命令路由实现。调用 sys_protocol 得到结构化命令，检查语言锁定，把命令转换为 SysEvent，并根据业务回执发送 ACK:OK/WARN/ERR 给 WebBLE。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_router.cpp
#include "sys_router.h"
#include "sys_protocol.h"
#include "sys_event.h"
#include "sys_ble.h"
#include "sys_command_result.h"
#include "../lang/terminal_lang.h"

#include "hal/hal.h"
#include "sys_specials.h"
#include "app_manager.h"
#include "sys_prescript_target.h"
#include <Arduino.h>

// ==========================================
// Stage 5：路由层只负责把结构化协议命令分发到系统事件。
// 具体字符串解析在 sys_protocol.cpp，业务处理仍由各 App / Sys 模块订阅事件完成。
// ==========================================

// 【函数说明】组装并发送 ACK 文本；可变更命令返回 OK/WARN/ERR，非变更查询命令成功时不发 ACK。
static void Router_SendAckResult(SysCommandType type, SysCmdResultLevel level, const String &code, const String &detail = String())
{
    if (!SysProtocol_IsMutating(type) && level != SysCmdResultLevel::Error)
        return;

    String out = "ACK:";
    switch (level)
    {
    case SysCmdResultLevel::Ok:
        out += "OK:";
        break;
    case SysCmdResultLevel::Warn:
        out += "WARN:";
        break;
    case SysCmdResultLevel::Error:
        out += "ERR:";
        break;
    case SysCmdResultLevel::None:
    default:
        out += "OK:";
        break;
    }

    out += SysProtocol_CommandName(type);
    out += ":";
    if (code.length() > 0)
        out += code;
    else
        out += "DISPATCHED";

    if (detail.length() > 0)
    {
        out += ":";
        out += detail;
    }

    SysBLE_Notify(out.c_str());
}

// 【函数说明】发送默认成功 ACK，常用于没有业务模块写回执的命令。
static void Router_SendAckOk(SysCommandType type, const String &code = "DISPATCHED")
{
    Router_SendAckResult(type, SysCmdResultLevel::Ok, code);
}

// 【函数说明】发送错误 ACK，协议无效、未知命令和分发失败都走这里。
static void Router_SendAckErr(SysCommandType type, const String &reason)
{
    Router_SendAckResult(type, SysCmdResultLevel::Error, reason);
}

// 【函数说明】绘制一次青色边框衰减动画，给 GET:SYNC 请求提供实体终端收到同步命令的视觉反馈。
static void Router_FlashSyncFrame()
{
    // 原有同步时边框闪烁反馈保留，但集中在一个函数里。
    for (int intensity = 255; intensity >= 0; intensity -= 20)
    {
        uint16_t g = (intensity * 63) / 255;
        uint16_t b = (intensity * 31) / 255;
        uint16_t neon_cyan = (g << 5) | b;
        HAL_Draw_Rect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), neon_cyan);
        HAL_Draw_Rect(1, 1, HAL_Get_Screen_Width() - 2, HAL_Get_Screen_Height() - 2, neon_cyan);
        HAL_Screen_Update();
        delay(15);
    }
}

// 【函数说明】把结构化命令转换成具体 SysEvent：ALM/SCH/PRE/COIN 等由对应 App 订阅处理，GET 类命令直接回传数据。
static bool SysRouter_Dispatch(const SysParsedCommand &cmd)
{
    switch (cmd.type)
    {
    case SysCommandType::GetSync:
    {
        SysBLE_Notify("SYNC:CLEAR");
        delay(50);
        SysEvent_Publish(EVT_BLE_SYNC_REQ, nullptr);
        Router_FlashSyncFrame();
        return true;
    }

    case SysCommandType::GetLanguage:
    {
        String out = "LANG:";
        SystemLang_t lang = appManager.getLanguage();
        out += TerminalLang::Code(lang);
        out += TerminalLang::LOCKED ? ":LOCKED:" : ":RUNTIME:";
        out += TerminalLang::BUILD_CODE;
        SysBLE_Notify(out.c_str());
        return true;
    }

    case SysCommandType::TextNotify:
    {
        Evt_Notify_t payload = {cmd.text.c_str(), true};
        SysEvent_Publish(EVT_NOTIFY_CUSTOM, &payload);
        return true;
    }

    case SysCommandType::AlarmAdd:
    {
        Evt_AlmAdd_t payload = {cmd.name.c_str(), cmd.hour, cmd.minute, cmd.text.c_str()};
        SysEvent_Publish(EVT_ALARM_ADD, &payload);
        return true;
    }

    case SysCommandType::AlarmDel:
    {
        Evt_AlmDel_t payload = {cmd.name.c_str()};
        SysEvent_Publish(EVT_ALARM_DEL, &payload);
        return true;
    }

    case SysCommandType::PomodoroUpdate:
    {
        Evt_PomUpd_t payload = {cmd.index, cmd.name.c_str(), cmd.work_min, cmd.rest_min};
        SysEvent_Publish(EVT_POMODORO_UPDATE, &payload);
        return true;
    }

    case SysCommandType::ScheduleAdd:
    {
        Evt_SchAdd_t payload = {cmd.timestamp, cmd.title.c_str(), cmd.text.c_str(), cmd.hidden};
        SysEvent_Publish(EVT_SCHEDULE_ADD, &payload);
        return true;
    }

    case SysCommandType::ScheduleDel:
    {
        Evt_SchDel_t payload = {cmd.title.c_str()};
        SysEvent_Publish(EVT_SCHEDULE_DEL, &payload);
        return true;
    }

    case SysCommandType::PrescriptAdd:
    {
        Evt_PreAdd_t payload = {cmd.lang, cmd.text.c_str()};
        SysEvent_Publish(EVT_PRESCRIPT_ADD, &payload);
        return true;
    }

    case SysCommandType::PrescriptDel:
    {
        Evt_PreDel_t payload = {cmd.lang, cmd.text.c_str()};
        SysEvent_Publish(EVT_PRESCRIPT_DEL, &payload);
        return true;
    }

    case SysCommandType::WifiSet:
    {
        Evt_WifiSet_t payload = {cmd.ssid.c_str(), cmd.pass.c_str()};
        SysEvent_Publish(EVT_WIFI_SET, &payload);
        Serial.printf("[路由中心] 收到网段授权: SSID=%s\n", cmd.ssid.c_str());
        return true;
    }

    case SysCommandType::CoinAdd:
    {
        Evt_CoinAdd_t payload = {cmd.base_power, cmd.coin_power, cmd.coin_count, cmd.colors.c_str(), cmd.name.c_str()};
        SysEvent_Publish(EVT_COIN_PRESET_ADD, &payload);
        Serial.printf("[路由中心] 添加硬币技能: %s\n", cmd.name.c_str());
        return true;
    }

    case SysCommandType::CoinDel:
    {
        Evt_CoinDel_t payload = {cmd.name.c_str()};
        SysEvent_Publish(EVT_COIN_PRESET_DEL, &payload);
        Serial.printf("[路由中心] 抹除硬币技能: %s\n", cmd.name.c_str());
        return true;
    }

    case SysCommandType::SpecialForce:
    {
        Evt_SpcForce_t payload = {cmd.id.c_str()};
        SysEvent_Publish(EVT_SPECIAL_FORCE, &payload);
        Serial.printf("[路由中心] 收到特异点强行触发指令: ID=%s\n", cmd.id.c_str());
        return true;
    }

    case SysCommandType::GetSpecialText:
    {
        extern SysSpecials sysSpecials;
        sysSpecials.syncTextByID(cmd.id);
        return true;
    }

    case SysCommandType::GetTarget:
    {
        SysPrescriptTarget_SyncBLE();
        return true;
    }

    case SysCommandType::TargetAdd:
    {
        SysPrescriptTarget_Add(cmd.id);
        SysPrescriptTarget_SyncBLE();
        return true;
    }

    case SysCommandType::TargetDel:
    {
        SysPrescriptTarget_Delete(cmd.id);
        SysPrescriptTarget_SyncBLE();
        return true;
    }

    case SysCommandType::TargetSet:
    {
        SysPrescriptTarget_SetCurrent(cmd.id);
        SysPrescriptTarget_SyncBLE();
        return true;
    }

    case SysCommandType::Invalid:
    case SysCommandType::Unknown:
    default:
        return false;
    }
}

// 【函数说明】执行单条子命令：解析、语言锁定检查、开启业务结果捕获、分发事件、根据结果回 ACK。
static void SysRouter_ExecuteSingle(const String &raw_cmd)
{
    SysParsedCommand cmd = SysProtocol_ParseSingle(raw_cmd);

    if (cmd.type == SysCommandType::Invalid)
    {
        Serial.printf("[协议层] 无效命令: %s | reason=%s\n", raw_cmd.c_str(), cmd.error.c_str());
        Router_SendAckErr(cmd.type, cmd.error);
        return;
    }

    if (cmd.type == SysCommandType::Unknown)
    {
        Serial.printf("[协议层] 未知命令: %s\n", raw_cmd.c_str());
        Router_SendAckErr(cmd.type, "UNKNOWN_COMMAND");
        return;
    }

    // Stage 6B：锁定语言固件拒绝写入另一语言的指令库，避免中文/英文版本互相污染。
    if ((cmd.type == SysCommandType::PrescriptAdd || cmd.type == SysCommandType::PrescriptDel) && !TerminalLang::Accepts((SystemLang_t)cmd.lang))
    {
        Serial.printf("[协议层] 语言锁定：拒绝 %s 命令写入 %s 库，当前固件=%s\n",
                      SysProtocol_CommandName(cmd.type),
                      TerminalLang::Code((SystemLang_t)cmd.lang),
                      TerminalLang::BUILD_CODE);
        Router_SendAckResult(cmd.type, SysCmdResultLevel::Error, "LANG_LOCKED", TerminalLang::BUILD_CODE);
        return;
    }

    if (SysProtocol_IsMutating(cmd.type))
        SysCmdResult_Begin();

    bool ok = SysRouter_Dispatch(cmd);
    if (!ok)
    {
        if (SysProtocol_IsMutating(cmd.type))
            SysCmdResult_Cancel();
        Router_SendAckErr(cmd.type, "DISPATCH_FAILED");
        return;
    }

    if (SysProtocol_IsMutating(cmd.type))
    {
        SysCmdResult result;
        if (SysCmdResult_Take(result))
        {
            Router_SendAckResult(cmd.type, result.level, result.code, result.detail);
        }
        else
        {
            // 对于 TXT/SPC 这类没有持久化保存动作的命令，DISPATCHED 表示已成功送达事件总线。
            Router_SendAckOk(cmd.type, "DISPATCHED");
        }
    }
}

// ==========================================
// 旧宏协议兼容入口：支持换行与 | 连发。
// ==========================================
// 【函数说明】处理一段来自 BLE/NFC 的原始文本；先拆宏命令，再逐条执行并留出 20ms 给 BLE/FreeRTOS 调度。
void SysRouter_ProcessBLE(const String &msg)
{
    int cursor = 0;
    String cmd;
    while (SysProtocol_NextMacroCommand(msg, cursor, cmd))
    {
        Serial.printf("[宏引擎] 提取子指令并开火: %s\n", cmd.c_str());
        SysRouter_ExecuteSingle(cmd);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ==========================================
// 网络 API 隐秘指令入口
// ==========================================
// 【函数说明】把网络 API 拉取到的隐藏日程直接发布为 SCHEDULE_ADD，隐藏标志固定为 true。
void SysRouter_ProcessAPI(uint32_t tt, const String &title, const String &text)
{
    if (tt > 0)
    {
        Evt_SchAdd_t payload = {tt, title.c_str(), text.c_str(), true};
        SysEvent_Publish(EVT_SCHEDULE_ADD, &payload);
    }
}

// ==========================================
// NFC 物理卡片路由分发中心
// ==========================================
// 【函数说明】NFC 扫描事件回调；把实体卡读出的文本交给 BLE 协议入口，实现 NFC 与 WebBLE 共用协议。
void _Cb_NfcScanned(void *payload)
{
    Evt_NfcScanned_t *p = (Evt_NfcScanned_t *)payload;
    String text = String(p->payload);

    Serial.printf("[路由中心] 收到 NFC 物理卡片指令: %s\n", text.c_str());
    SysRouter_ProcessBLE(text);
}

// 【函数说明】订阅 EVT_NFC_SCANNED，让 NFC 后台任务读到卡片后能进入路由层。
void SysRouter_Init()
{
    SysEvent_Subscribe(EVT_NFC_SCANNED, _Cb_NfcScanned);
}
