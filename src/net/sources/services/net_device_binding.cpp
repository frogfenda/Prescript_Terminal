/*
【模块职责】执行设备直连绑定：领取挑战、生成 HMAC 校验码、接收并持久化永久通行码、建立临时会话。
【恢复语义】服务端在确认前会为同一机器返回相同的首版通行码，因此响应中途丢失后可在下轮安全重试。
*/
#include "net/services/net_device_binding.h"

#include <ArduinoJson.h>
#include "net/net_auth_session.h"
#include "net/net_http_transport.h"
#include "net/net_task_registry.h"
#include "sys/sys_device_binding.h"
#include "sys/sys_device_identity.h"

namespace
{
    constexpr uint16_t kTaskId = 2;
    constexpr size_t kBindingResponseLimit = 1024;

    NetTaskExecutionResult TemporaryFailure(const char *stage, NetHttpResult result)
    {
        Serial.printf("[设备绑定] %s失败，传输结果=%u。\n",
                      stage,
                      static_cast<unsigned>(result));
        return {NetTaskDisposition::Retry, 5UL * 60UL};
    }

    NetTaskExecutionResult EnsureAuthentication(const NetTaskContext &context)
    {
        const NetAuthEnsureResult auth = NetAuthSession_Ensure(context);
        if (auth == NetAuthEnsureResult::Ready)
            return {NetTaskDisposition::Complete, 0};
        if (auth == NetAuthEnsureResult::CredentialsRejected ||
            auth == NetAuthEnsureResult::DeviceUnbound)
            return {NetTaskDisposition::AuthBlocked, 0};
        return {NetTaskDisposition::Retry, 5UL * 60UL};
    }

    NetTaskExecutionResult Execute(const NetTaskInvocation &, const NetTaskContext &context)
    {
        const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
        if (!identity.storage_ready || identity.machine_code.isEmpty())
        {
            Serial.println("[设备绑定] 身份分区或机器码不可用，本轮停止认证。 ");
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        if (identity.provisioned)
            return EnsureAuthentication(context);

        if (!SysDeviceBinding_IsProofAvailable())
        {
            Serial.println("[设备绑定] 固件未注入产品绑定主密钥，自动绑定不可用。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }

        JsonDocument challenge_request;
        challenge_request["machine_code"] = identity.machine_code;
        String challenge_body;
        serializeJson(challenge_request, challenge_body);

        NetHttpResponse challenge_response;
        NetHttpResult transport = NetHttp_PostJson(
            "/api/v1/device/binding/challenge",
            challenge_body,
            String(),
            context,
            kBindingResponseLimit,
            challenge_response);
        if (transport != NetHttpResult::Ok)
            return TemporaryFailure("领取绑定挑战", transport);
        if (challenge_response.status_code == 409)
        {
            Serial.println("[设备绑定] 服务端记录已绑定，但本机没有凭据，需要管理员恢复。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (challenge_response.status_code != 201)
        {
            Serial.printf("[设备绑定] 领取挑战失败，HTTP=%d。\n", challenge_response.status_code);
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        JsonDocument challenge_document;
        DeserializationError error = deserializeJson(challenge_document, challenge_response.body);
        const int protocol_version = challenge_document["protocol_version"] | 0;
        const String challenge_id = challenge_document["challenge_id"] | "";
        const String challenge = challenge_document["challenge"] | "";
        if (error || protocol_version != 1 || challenge_id.length() != 36 || challenge.isEmpty())
        {
            Serial.println("[设备绑定] 挑战响应格式无效。 ");
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        String verification_code;
        if (!SysDeviceBinding_BuildVerificationCode(
                identity.machine_code,
                challenge_id,
                challenge,
                verification_code))
        {
            Serial.println("[设备绑定] 无法生成挑战校验码。 ");
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        JsonDocument complete_request;
        complete_request["machine_code"] = identity.machine_code;
        complete_request["challenge_id"] = challenge_id;
        complete_request["verification_code"] = verification_code;
        String complete_body;
        serializeJson(complete_request, complete_body);
        verification_code = "";

        NetHttpResponse complete_response;
        transport = NetHttp_PostJson(
            "/api/v1/device/binding/complete",
            complete_body,
            String(),
            context,
            kBindingResponseLimit,
            complete_response);
        complete_body = "";
        if (transport != NetHttpResult::Ok)
            return TemporaryFailure("提交绑定证明", transport);
        if (complete_response.status_code == 401 || complete_response.status_code == 403)
        {
            Serial.println("[设备绑定] 服务端拒绝绑定证明。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (complete_response.status_code != 200)
        {
            Serial.printf("[设备绑定] 完成绑定失败，HTTP=%d。\n", complete_response.status_code);
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        JsonDocument complete_document;
        error = deserializeJson(complete_document, complete_response.body);
        const int completed_protocol = complete_document["protocol_version"] | 0;
        const String public_id = complete_document["public_id"] | "";
        String device_key = complete_document["device_key"] | "";
        const uint32_t credential_version = complete_document["credential_version"] | 0;
        if (error || completed_protocol != 1 || credential_version != 1 ||
            public_id.isEmpty() || device_key.isEmpty())
        {
            device_key = "";
            Serial.println("[设备绑定] 永久凭据响应格式无效。 ");
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        const SysDeviceIdentityBindResult saved = SysDeviceIdentity_Bind(public_id, device_key);
        device_key = "";
        if (saved != SysDeviceIdentityBindResult::Ok)
        {
            Serial.printf("[设备绑定] 永久凭据落盘失败，结果=%u；未向服务端确认。\n",
                          static_cast<unsigned>(saved));
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        /* 服务端把首次使用永久通行码成功登录视为落盘确认；响应若丢失则不会走到这里。 */
        const NetTaskExecutionResult authenticated = EnsureAuthentication(context);
        if (authenticated.disposition != NetTaskDisposition::Complete)
            return authenticated;

        Serial.printf("[设备绑定] 自动绑定完成，公开身份=%s；永久通行码未写入日志。\n",
                      public_id.c_str());
        return {NetTaskDisposition::Complete, 0};
    }
}

bool NetDeviceBinding_Register()
{
    NetTaskDefinition definition;
    definition.task_id = kTaskId;
    definition.name = "设备绑定与认证";
    definition.execute = Execute;
    definition.trigger_mask = NET_TASK_TRIGGER_SESSION_PREPARE;
    return NetTaskRegistry_Register(definition);
}
