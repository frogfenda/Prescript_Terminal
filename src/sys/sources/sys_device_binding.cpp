/*
【模块职责】实现设备身份绑定协议 v1 的两级 HMAC-SHA256 与 Base64URL 编码。
【密钥模型】当前测试阶段由同批固件共享产品绑定主密钥，再按机器码派生 claim key；
量产时应把输入替换为工厂逐机写入且受 eFuse/Flash Encryption 保护的设备秘密。
*/
#include "sys/sys_device_binding.h"
#include "sys/sys_device_identity.h"
#include "sys/sys_command_registry.h"
#include "sys/sys_psram_json.h"
#include "sys/sys_sleep_scheduler.h"

#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include "generated_binding_secret.h"

namespace
{
    constexpr uint32_t kBindingModeTimeoutMs = 10UL * 60UL * 1000UL;
    bool s_modeActive = false;
    bool s_commitAllowed = false;
    uint32_t s_modeStartedMs = 0;
    String s_lastChallengeId;
    String s_lastChallenge;
    constexpr char kClaimKeyDomain[] = "PRESCRIPT-CLAIM-KEY-V1\n";
    constexpr char kProofDomain[] = "PRESCRIPT-BIND-PROOF-V1\n";

    bool HmacSha256(
        const uint8_t *key,
        size_t key_length,
        const uint8_t *message,
        size_t message_length,
        uint8_t output[32])
    {
        const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        return info &&
               mbedtls_md_hmac(info, key, key_length, message, message_length, output) == 0;
    }

    String EncodeBase64Url(const uint8_t *value, size_t value_length)
    {
        uint8_t encoded[48] = {};
        size_t encoded_length = 0;
        if (mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_length, value, value_length) != 0)
            return String();

        while (encoded_length > 0 && encoded[encoded_length - 1] == '=')
            --encoded_length;
        for (size_t index = 0; index < encoded_length; ++index)
        {
            if (encoded[index] == '+')
                encoded[index] = '-';
            else if (encoded[index] == '/')
                encoded[index] = '_';
        }
        encoded[encoded_length] = '\0';
        return String(reinterpret_cast<const char *>(encoded));
    }

    // 向客户端推送 EVT:BIND.MODE:{"active":true/false}。
    void NotifyModeChanged(bool active)
    {
        JsonDocument document(SysPsramJsonAllocator::Instance());
        document["active"] = active;
        SysCommand_Event("BIND.MODE", document);
    }
}

void SysDeviceBinding_EnterMode()
{
    s_modeActive = true;
    s_commitAllowed = false;
    s_modeStartedMs = millis();
    s_lastChallengeId = "";
    s_lastChallenge = "";
    SysSleep_SetBlocker(SysSleepBlocker::BindingMode, true);
    Serial.println("[设备绑定] 已进入账号认领模式，等待 App 完成账号流程。");
    NotifyModeChanged(true);
}

void SysDeviceBinding_ExitMode()
{
    const bool was_active = s_modeActive;
    s_modeActive = false;
    s_commitAllowed = false;
    s_lastChallengeId = "";
    s_lastChallenge = "";
    SysSleep_SetBlocker(SysSleepBlocker::BindingMode, false);
    // 只有真正从开启变为关闭时才推送，页面反复调用 ExitMode 不会重复推送。
    if (was_active)
        NotifyModeChanged(false);
}

void SysDeviceBinding_Update()
{
    if (s_modeActive && millis() - s_modeStartedMs >= kBindingModeTimeoutMs)
    {
        SysDeviceBinding_ExitMode();
        Serial.println("[设备绑定] 账号认领模式已超时关闭。");
    }
}

bool SysDeviceBinding_IsModeActive()
{
    return s_modeActive;
}

bool SysDeviceBinding_BuildModeProof(
    const String &challenge_id,
    const String &challenge,
    String &verification_code)
{
    verification_code = "";
    if (!s_modeActive || challenge_id.isEmpty() || challenge.isEmpty())
        return false;
    const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
    if (!SysDeviceBinding_BuildVerificationCode(
            identity.machine_code, challenge_id, challenge, verification_code))
        return false;
    s_lastChallengeId = challenge_id;
    s_lastChallenge = challenge;
    s_commitAllowed = true;
    return true;
}

bool SysDeviceBinding_IsCredentialCommitAllowed()
{
    return s_modeActive && s_commitAllowed;
}

void SysDeviceBinding_ConsumeCredentialCommit()
{
    s_commitAllowed = false;
    s_lastChallengeId = "";
    s_lastChallenge = "";
}

bool SysDeviceBinding_IsProofAvailable()
{
    return strlen(PRESCRIPT_DEVICE_BINDING_MASTER_SECRET) >= 32;
}

bool SysDeviceBinding_BuildVerificationCode(
    const String &machine_code,
    const String &challenge_id,
    const String &challenge,
    String &verification_code)
{
    verification_code = "";
    if (!SysDeviceBinding_IsProofAvailable() || machine_code.isEmpty() ||
        challenge_id.isEmpty() || challenge.isEmpty())
        return false;

    const String claim_message = String(kClaimKeyDomain) + machine_code;
    uint8_t claim_key[32] = {};
    if (!HmacSha256(
            reinterpret_cast<const uint8_t *>(PRESCRIPT_DEVICE_BINDING_MASTER_SECRET),
            strlen(PRESCRIPT_DEVICE_BINDING_MASTER_SECRET),
            reinterpret_cast<const uint8_t *>(claim_message.c_str()),
            claim_message.length(),
            claim_key))
        return false;

    const String proof_message =
        String(kProofDomain) + machine_code + "\n" + challenge_id + "\n" + challenge;
    uint8_t proof[32] = {};
    const bool calculated = HmacSha256(
        claim_key,
        sizeof(claim_key),
        reinterpret_cast<const uint8_t *>(proof_message.c_str()),
        proof_message.length(),
        proof);
    memset(claim_key, 0, sizeof(claim_key));
    if (!calculated)
        return false;

    verification_code = EncodeBase64Url(proof, sizeof(proof));
    memset(proof, 0, sizeof(proof));
    return verification_code.length() == 43;
}

// ===== 【蓝牙命令区】BIND 设备绑定 =====
// 只允许蓝牙来源：NFC 卡片和串口都不能参与绑定。处理函数只调用绑定与身份模块的接口。

static const SysCmdArgSpec kBindProofArgs[] = {
    SYS_ARG_STR("challenge_id", 1, 64, ARG_TRIM),
    SYS_ARG_REST("challenge", 1, 256, ARG_TRIM),
};

static const SysCmdArgSpec kBindCommitArgs[] = {
    SYS_ARG_STR("public_id", 1, 64, ARG_TRIM),
    SYS_ARG_INT("version", 1, 1000000),
    SYS_ARG_REST("key", 1, 256, ARG_TRIM),
};

// BIND.INFO：绑定模式状态和可公开的身份字段。
static void Cmd_BindInfo(SysCmdCtx &ctx, const SysCmdArgs &)
{
    JsonDocument document(SysPsramJsonAllocator::Instance());
    document["mode_active"] = SysDeviceBinding_IsModeActive();
    SysDeviceIdentity_FillPublicJson(document);
    ctx.data(document);
    ctx.ok("OK");
}

// BIND.PROOF：绑定模式中为账号挑战生成一次性证明 → DAT {"challenge_id","code"}。
static void Cmd_BindProof(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    if (!SysDeviceBinding_IsModeActive())
    {
        ctx.error("BINDING_MODE_REQUIRED");
        return;
    }
    String code;
    if (!SysDeviceBinding_BuildModeProof(args.string(0), args.string(1), code))
    {
        ctx.error("PROOF_GENERATION_FAILED");
        return;
    }
    JsonDocument document(SysPsramJsonAllocator::Instance());
    document["challenge_id"] = args.str(0);
    document["code"] = code;
    ctx.data(document);
    ctx.ok("PROOF_READY");
}

// BIND.COMMIT：提交服务器分配的公开 ID 和设备通行码；必须紧跟一次成功的 BIND.PROOF。
static void Cmd_BindCommit(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    if (!SysDeviceBinding_IsCredentialCommitAllowed())
    {
        ctx.error("BINDING_PROOF_REQUIRED");
        return;
    }
    const SysDeviceIdentityBindResult result = SysDeviceIdentity_ReplaceBinding(
        args.string(0), args.string(2), static_cast<uint32_t>(args.num(1)));
    switch (result)
    {
    case SysDeviceIdentityBindResult::Ok:
        SysDeviceBinding_ConsumeCredentialCommit();
        ctx.ok("BOUND", args.string(0));
        break;
    case SysDeviceIdentityBindResult::AlreadyBound: ctx.ack(SysOpResult::Warn("ALREADY_BOUND")); break;
    case SysDeviceIdentityBindResult::InvalidPublicId: ctx.error("INVALID_PUBLIC_ID"); break;
    case SysDeviceIdentityBindResult::InvalidDeviceKey: ctx.error("INVALID_DEVICE_KEY"); break;
    case SysDeviceIdentityBindResult::MachineCodeUnavailable: ctx.error("MACHINE_CODE_UNAVAILABLE"); break;
    case SysDeviceIdentityBindResult::StorageUnavailable: ctx.error("IDENTITY_STORAGE_UNAVAILABLE"); break;
    case SysDeviceIdentityBindResult::SaveFailed:
    default: ctx.error("IDENTITY_SAVE_FAILED"); break;
    }
}

static const SysCmdSpec kBindCmds[] = {
    {"INFO", nullptr, 0, 0, Cmd_BindInfo, nullptr},
    {"PROOF", kBindProofArgs, SYS_COUNT_OF(kBindProofArgs), 0, Cmd_BindProof, nullptr},
    {"COMMIT", kBindCommitArgs, SYS_COUNT_OF(kBindCommitArgs), 0, Cmd_BindCommit, nullptr},
};

static const SysCmdGroup kBindGroup = {"BIND", 1, SRC_BLE, kBindCmds, SYS_COUNT_OF(kBindCmds)};

void SysDeviceBinding_RegisterCommands()
{
    SysCommand_RegisterGroup(&kBindGroup);
}

// ===== 【蓝牙命令区结束】 =====
