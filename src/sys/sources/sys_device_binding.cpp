/*
【模块职责】实现设备自动绑定协议 v1 的两级 HMAC-SHA256 与 Base64URL 编码。
【密钥模型】当前测试阶段由同批固件共享产品绑定主密钥，再按机器码派生 claim key；
量产时应把输入替换为工厂逐机写入且受 eFuse/Flash Encryption 保护的设备秘密。
*/
#include "sys/sys_device_binding.h"

#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include "generated_binding_secret.h"

namespace
{
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
