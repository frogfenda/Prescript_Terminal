/*
【模块职责】固件内部命令注册表实现。每个外部命令只有一个直接 Handler；Handler 复用
现有 App/SYS 领域接口，不再构造“命令型事件”再交给业务回调。
*/
#include "sys/sys_command_registry.h"
#include "sys/sys_ble.h"
#include "sys/sys_command_result.h"
#include "sys/sys_device_binding.h"
#include "sys/sys_device_identity.h"
#include "sys/sys_event.h"
#include "sys/sys_prescript_target.h"
#include "sys/sys_reminder.h"
#include "sys/sys_specials.h"
#include "sys/app_manager.h"
#include "net/net_service.h"
#include "lang/terminal_lang.h"
#include "hal/hal.h"

static String NormalizeSyncScope(const String &raw_scope)
{
    String scope = raw_scope;
    scope.trim();
    scope.toUpperCase();
    if (scope.isEmpty() || scope == "ZH" || scope == "EN" || scope == "ALL")
        return "ALL";
    // PRE 也是独立同步域；不能把 GET:SYNC:PRE 误判为 ALL，
    // 否则移动端只想刷新指令库时会收到整套回读数据。
    if (scope == "ALM" || scope == "SCH" || scope == "PRE" || scope == "COIN" ||
        scope == "POM" || scope == "SPC" || scope == "TGT")
        return scope;
    return "ALL";
}

static void FlashSyncFrame()
{
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

static bool HandleGetSync(const SysParsedCommand &command)
{
    String scope = NormalizeSyncScope(command.sync_lang);
    Serial.printf("[蓝牙同步] 收到本机回读请求，范围=%s。\n", scope.c_str());
    String clear = (scope == "ALL") ? "SYNC:CLEAR" : "SYNC:CLEAR:" + scope;
    SysBLE_Notify(clear.c_str());
    delay(50);
    Evt_BleSyncReq_t payload = {scope.c_str()};
    SysEvent_Publish(EVT_BLE_SYNC_REQ, &payload);
    FlashSyncFrame();
    Serial.printf("[蓝牙同步] 本机回读广播完成，范围=%s。\n", scope.c_str());
    return true;
}

static bool HandleGetLanguage(const SysParsedCommand &)
{
    String out = "LANG:";
    SystemLang_t lang = appManager.getLanguage();
    out += TerminalLang::Code(lang);
    out += TerminalLang::LOCKED ? ":LOCKED:" : ":RUNTIME:";
    out += TerminalLang::BUILD_CODE;
    SysBLE_Notify(out.c_str());
    return true;
}

static bool HandleTextNotify(const SysParsedCommand &command)
{
    SysReminder_Submit(SysReminderKind::Custom, command.text.c_str(), true);
    return true;
}

static bool HandleGetIdentity(const SysParsedCommand &)
{
    SysBLE_Notify(SysDeviceBinding_IsModeActive() ? "BIND_MODE:ACTIVE" : "BIND_MODE:INACTIVE");
    delay(50);
    const String response = SysDeviceIdentity_BuildPublicResponse();
    SysBLE_Notify(response.c_str());
    Serial.printf("[设备绑定] 已回复实时绑定状态和设备身份信息，长度=%u。\n", static_cast<unsigned>(response.length()));
    return true;
}

static bool HandleBindingProof(const SysParsedCommand &command)
{
    if (!SysDeviceBinding_IsModeActive())
    {
        SysCmdResult_Error("BINDING_MODE_REQUIRED");
        return true;
    }
    String verification_code;
    if (!SysDeviceBinding_BuildModeProof(command.challenge_id, command.challenge, verification_code))
    {
        SysCmdResult_Error("PROOF_GENERATION_FAILED");
        return true;
    }
    String response = "BIND_PROOF_RESULT:" + command.challenge_id + ":" + verification_code;
    SysBLE_Notify(response.c_str());
    SysCmdResult_Ok("PROOF_READY");
    return true;
}

static bool HandleDeviceBind(const SysParsedCommand &command)
{
    if (!SysDeviceBinding_IsCredentialCommitAllowed())
    {
        SysCmdResult_Error("BINDING_PROOF_REQUIRED");
        return true;
    }
    const SysDeviceIdentityBindResult result = SysDeviceIdentity_ReplaceBinding(
        command.public_id, command.device_key, command.credential_version);
    switch (result)
    {
    case SysDeviceIdentityBindResult::Ok:
        SysDeviceBinding_ConsumeCredentialCommit();
        SysCmdResult_Ok("BOUND", command.public_id);
        break;
    case SysDeviceIdentityBindResult::AlreadyBound: SysCmdResult_Warn("ALREADY_BOUND"); break;
    case SysDeviceIdentityBindResult::InvalidPublicId: SysCmdResult_Error("INVALID_PUBLIC_ID"); break;
    case SysDeviceIdentityBindResult::InvalidDeviceKey: SysCmdResult_Error("INVALID_DEVICE_KEY"); break;
    case SysDeviceIdentityBindResult::MachineCodeUnavailable: SysCmdResult_Error("MACHINE_CODE_UNAVAILABLE"); break;
    case SysDeviceIdentityBindResult::StorageUnavailable: SysCmdResult_Error("IDENTITY_STORAGE_UNAVAILABLE"); break;
    case SysDeviceIdentityBindResult::SaveFailed:
    default: SysCmdResult_Error("IDENTITY_SAVE_FAILED"); break;
    }
    return true;
}

static bool HandleGetSpecialText(const SysParsedCommand &command)
{
    sysSpecials.syncTextByID(command.id);
    return true;
}

static bool HandleGetTarget(const SysParsedCommand &)
{
    SysPrescriptTarget_SyncBLE();
    return true;
}

static bool HandleTargetAdd(const SysParsedCommand &command)
{
    SysPrescriptTarget_Add(command.id);
    SysPrescriptTarget_SyncBLE();
    return true;
}

static bool HandleTargetDelete(const SysParsedCommand &command)
{
    SysPrescriptTarget_Delete(command.id);
    SysPrescriptTarget_SyncBLE();
    return true;
}

static bool HandleTargetSet(const SysParsedCommand &command)
{
    SysPrescriptTarget_SetCurrent(command.id);
    SysPrescriptTarget_SyncBLE();
    return true;
}

static bool HandleAlarmAdd(const SysParsedCommand &command)
{
    Alarm_AddPresetMobile(command.name.c_str(), command.hour, command.minute, command.text.c_str());
    return true;
}

static bool HandleAlarmDelete(const SysParsedCommand &command)
{
    Alarm_DeleteMobile(command.name.c_str());
    return true;
}

static bool HandlePomodoro(const SysParsedCommand &command)
{
    Pomodoro_UpdatePreset(command.index, command.name.c_str(), command.work_min, command.rest_min);
    return true;
}

static bool HandleScheduleAdd(const SysParsedCommand &command)
{
    Schedule_AddMobile(command.timestamp, command.title.c_str(), command.text.c_str(), command.hidden);
    return true;
}

static bool HandleScheduleDelete(const SysParsedCommand &command)
{
    Schedule_DeleteMobile(command.title.c_str());
    return true;
}

static bool HandlePrescriptAdd(const SysParsedCommand &command)
{
    DBArchive_AddCommand((SystemLang_t)command.lang, command.text);
    return true;
}

static bool HandlePrescriptDelete(const SysParsedCommand &command)
{
    DBArchive_DeleteCommand((SystemLang_t)command.lang, command.text);
    return true;
}

static bool HandleWifi(const SysParsedCommand &command)
{
    NetService_SetWifiCommand(command.ssid.c_str(), command.pass.c_str());
    return true;
}

static bool HandleCoinAdd(const SysParsedCommand &command)
{
    Coin_AddPresetMobile(command.base_power, command.coin_power, command.coin_count,
                         command.colors.c_str(), command.name.c_str());
    return true;
}

static bool HandleCoinDelete(const SysParsedCommand &command)
{
    Coin_DeletePresetMobile(command.name.c_str());
    return true;
}

static bool HandleSpecialForce(const SysParsedCommand &command)
{
    PushNotify_Trigger_Special_Command(command.id.c_str());
    return true;
}

static const SysCommandDescriptor kCommands[] = {
    {SysCommandType::GetSync, "GET_SYNC", false, HandleGetSync},
    {SysCommandType::GetLanguage, "GET_LANG", false, HandleGetLanguage},
    {SysCommandType::TextNotify, "TXT", true, HandleTextNotify},
    {SysCommandType::AlarmAdd, "ALM", true, HandleAlarmAdd},
    {SysCommandType::AlarmDel, "ALM_DEL", true, HandleAlarmDelete},
    {SysCommandType::PomodoroUpdate, "POM", true, HandlePomodoro},
    {SysCommandType::ScheduleAdd, "SCH", true, HandleScheduleAdd},
    {SysCommandType::ScheduleDel, "SCH_DEL", true, HandleScheduleDelete},
    {SysCommandType::PrescriptAdd, "PRE", true, HandlePrescriptAdd},
    {SysCommandType::PrescriptDel, "PRE_DEL", true, HandlePrescriptDelete},
    {SysCommandType::WifiSet, "WIFI", true, HandleWifi},
    {SysCommandType::CoinAdd, "COIN", true, HandleCoinAdd},
    {SysCommandType::CoinDel, "COIN_DEL", true, HandleCoinDelete},
    {SysCommandType::SpecialForce, "SPC", true, HandleSpecialForce},
    {SysCommandType::GetSpecialText, "GET_SPC_TXT", false, HandleGetSpecialText},
    {SysCommandType::GetTarget, "GET_TGT", false, HandleGetTarget},
    {SysCommandType::TargetAdd, "TGT_ADD", true, HandleTargetAdd},
    {SysCommandType::TargetDel, "TGT_DEL", true, HandleTargetDelete},
    {SysCommandType::TargetSet, "TGT_SET", true, HandleTargetSet},
    {SysCommandType::GetIdentity, "GET_IDENTITY", false, HandleGetIdentity},
    {SysCommandType::BindingProof, "BIND_PROOF", true, HandleBindingProof},
    {SysCommandType::DeviceBindV2, "DEV_BIND_V2", true, HandleDeviceBind},
};

const SysCommandDescriptor *SysCommandRegistry_Find(SysCommandType type)
{
    for (const SysCommandDescriptor &descriptor : kCommands)
        if (descriptor.type == type)
            return &descriptor;
    return nullptr;
}

bool SysCommandRegistry_Dispatch(const SysParsedCommand &command)
{
    const SysCommandDescriptor *descriptor = SysCommandRegistry_Find(command.type);
    return descriptor != nullptr && descriptor->handler != nullptr && descriptor->handler(command);
}

const char *SysCommandRegistry_CommandName(SysCommandType type)
{
    const SysCommandDescriptor *descriptor = SysCommandRegistry_Find(type);
    if (descriptor != nullptr)
        return descriptor->ack_name;
    return type == SysCommandType::Invalid ? "INVALID" : "UNKNOWN";
}

bool SysCommandRegistry_IsMutating(SysCommandType type)
{
    const SysCommandDescriptor *descriptor = SysCommandRegistry_Find(type);
    return descriptor != nullptr && descriptor->mutating;
}
