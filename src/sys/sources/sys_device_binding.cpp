/*
【模块职责】实现设备身份绑定协议 v1 的两级 HMAC-SHA256 与 Base64URL 编码。
【密钥模型】当前测试阶段由同批固件共享产品绑定主密钥，再按机器码派生 claim key；
量产时应把输入替换为工厂逐机写入且受 eFuse/Flash Encryption 保护的设备秘密。
*/
#include "sys/sys_device_binding.h"
#include "sys/sys_device_identity.h"
#include "sys/sys_ble.h"
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
}

void SysDeviceBinding_ExitMode()
{
    s_modeActive = false;
    s_commitAllowed = false;
    s_lastChallengeId = "";
    s_lastChallenge = "";
    SysSleep_SetBlocker(SysSleepBlocker::BindingMode, false);
}

void SysDeviceBinding_Update()
{
    if (s_modeActive && millis() - s_modeStartedMs >= kBindingModeTimeoutMs)
    {
        SysDeviceBinding_ExitMode();
        SysBLE_Notify("BIND_MODE:INACTIVE");
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
