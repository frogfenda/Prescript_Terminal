/*
【模块职责】执行设备直连绑定或服务器恢复绑定：领取挑战、生成 HMAC 校验码、原子保存永久通行码、建立临时会话。
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
    constexpr size_t kBindingResponseLimit = 1024;
    NetDeviceBindingFailure s_lastFailure = NetDeviceBindingFailure::None;
    portMUX_TYPE s_failureMux = portMUX_INITIALIZER_UNLOCKED;

    /** 绑定执行器运行在 Core 0，页面在主循环读取；短临界区保证失败类型不会跨核撕裂。 */
    void SetFailure(NetDeviceBindingFailure failure)
    {
        portENTER_CRITICAL(&s_failureMux);
        s_lastFailure = failure;
        portEXIT_CRITICAL(&s_failureMux);
    }

    const char *DescribeBindingFailure(NetDeviceBindingFailure failure)
    {
        switch (failure)
        {
        case NetDeviceBindingFailure::RequestTimeout:
            return "请求超时";
        case NetDeviceBindingFailure::SecureConnectionFailed:
            return "安全连接失败";
        case NetDeviceBindingFailure::ServerConnectionFailed:
            return "无法连接绑定服务器";
        case NetDeviceBindingFailure::RequestSendFailed:
            return "绑定请求发送失败";
        case NetDeviceBindingFailure::ConnectionLost:
            return "连接中途断开";
        case NetDeviceBindingFailure::InsufficientMemory:
            return "安全连接内存不足";
        case NetDeviceBindingFailure::InvalidServerResponse:
            return "服务器响应无效";
        case NetDeviceBindingFailure::NetworkTransportFailed:
        default:
            return "网络传输失败";
        }
    }

    NetDeviceBindingFailure TransportFailure(
        NetHttpResult result,
        const NetHttpResponse &response)
    {
        switch (result)
        {
        case NetHttpResult::DeadlineExceeded:
            return NetDeviceBindingFailure::RequestTimeout;
        case NetHttpResult::TlsInitializationFailed:
            return NetDeviceBindingFailure::SecureConnectionFailed;
        case NetHttpResult::ResponseTooLarge:
            return NetDeviceBindingFailure::InvalidServerResponse;
        case NetHttpResult::TransportFailed:
            switch (response.failure_detail)
            {
            case NetHttpFailureDetail::WifiUnavailable:
                return NetDeviceBindingFailure::NetworkTransportFailed;
            case NetHttpFailureDetail::DnsResolutionFailed:
            case NetHttpFailureDetail::TcpConnectionFailed:
                return NetDeviceBindingFailure::ServerConnectionFailed;
            case NetHttpFailureDetail::SecureConnectionFailed:
                return NetDeviceBindingFailure::SecureConnectionFailed;
            case NetHttpFailureDetail::ReadTimeout:
                return NetDeviceBindingFailure::RequestTimeout;
            case NetHttpFailureDetail::ServerConnectionFailed:
                return NetDeviceBindingFailure::ServerConnectionFailed;
            case NetHttpFailureDetail::RequestSendFailed:
                return NetDeviceBindingFailure::RequestSendFailed;
            case NetHttpFailureDetail::ConnectionLost:
                return NetDeviceBindingFailure::ConnectionLost;
            case NetHttpFailureDetail::InsufficientMemory:
                return NetDeviceBindingFailure::InsufficientMemory;
            case NetHttpFailureDetail::None:
            case NetHttpFailureDetail::Unknown:
            default:
                return NetDeviceBindingFailure::NetworkTransportFailed;
            }
        default:
            return NetDeviceBindingFailure::NetworkTransportFailed;
        }
    }

    NetTaskExecutionResult TemporaryFailure(
        const char *stage,
        NetHttpResult result,
        const NetHttpResponse &response)
    {
        const NetDeviceBindingFailure failure = TransportFailure(result, response);
        SetFailure(failure);
        Serial.printf("[设备绑定] %s失败：%s（类型=%u）。\n",
                      stage,
                      DescribeBindingFailure(failure),
                      static_cast<unsigned>(failure));
        return {NetTaskDisposition::Retry, 5UL * 60UL};
    }

    NetTaskExecutionResult EnsureAuthentication(const NetTaskContext &context)
    {
        const NetAuthEnsureResult auth = NetAuthSession_Ensure(context);
        if (auth == NetAuthEnsureResult::Ready)
            return {NetTaskDisposition::Complete, 0};
        SetFailure(NetDeviceBindingFailure::SessionAuthenticationFailed);
        if (auth == NetAuthEnsureResult::CredentialsRejected ||
            auth == NetAuthEnsureResult::DeviceUnbound)
            return {NetTaskDisposition::AuthBlocked, 0};
        return {NetTaskDisposition::Retry, 5UL * 60UL};
    }

    NetTaskExecutionResult ExecuteBinding(const NetTaskInvocation &, const NetTaskContext &context)
    {
        SetFailure(NetDeviceBindingFailure::Unknown);
        const SysDeviceIdentitySnapshot identity = SysDeviceIdentity_GetSnapshot();
        if (!identity.storage_ready || identity.machine_code.isEmpty())
        {
            SetFailure(NetDeviceBindingFailure::IdentityUnavailable);
            Serial.println("[设备绑定] 身份分区或机器码不可用，本轮停止认证。 ");
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        /*
         * 八秒恢复绑定可能紧接在一次成功在线验证之后，此时 RAM 仍有旧 Token。
         * 显式绑定必须清除它，确保服务器下发的新永久凭据在落盘后真正重新登录验证。
         */
        NetAuthSession_Clear();

        if (!SysDeviceBinding_IsProofAvailable())
        {
            SetFailure(NetDeviceBindingFailure::BindingSecretUnavailable);
            Serial.println("[设备绑定] 固件未注入产品绑定主密钥，身份绑定不可用。 ");
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
            return TemporaryFailure("领取绑定挑战", transport, challenge_response);
        if (challenge_response.status_code == 409)
        {
            SetFailure(NetDeviceBindingFailure::ServerAlreadyBound);
            Serial.println("[设备绑定] 服务端记录已绑定，但本机没有凭据，需要管理员恢复。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (challenge_response.status_code == 403)
        {
            SetFailure(NetDeviceBindingFailure::DeviceDisabled);
            Serial.println("[设备绑定] 此机器码已被服务器禁用，禁止重新绑定。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (challenge_response.status_code != 201)
        {
            SetFailure(challenge_response.status_code >= 500
                           ? NetDeviceBindingFailure::ServerUnavailable
                           : NetDeviceBindingFailure::ServerRejected);
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
            SetFailure(NetDeviceBindingFailure::InvalidServerResponse);
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
            SetFailure(NetDeviceBindingFailure::ProofGenerationFailed);
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
            return TemporaryFailure("提交绑定证明", transport, complete_response);
        if (complete_response.status_code == 401 || complete_response.status_code == 403)
        {
            SetFailure(complete_response.status_code == 403
                           ? NetDeviceBindingFailure::DeviceDisabled
                           : NetDeviceBindingFailure::ServerRejected);
            Serial.println(complete_response.status_code == 403
                               ? "[设备绑定] 此机器码已被服务器禁用，绑定证明被拒绝。 "
                               : "[设备绑定] 服务端拒绝绑定证明。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (complete_response.status_code != 200)
        {
            SetFailure(complete_response.status_code >= 500
                           ? NetDeviceBindingFailure::ServerUnavailable
                           : NetDeviceBindingFailure::ServerRejected);
            Serial.printf("[设备绑定] 完成绑定失败，HTTP=%d。\n", complete_response.status_code);
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        JsonDocument complete_document;
        error = deserializeJson(complete_document, complete_response.body);
        const int completed_protocol = complete_document["protocol_version"] | 0;
        const String public_id = complete_document["public_id"] | "";
        String device_key = complete_document["device_key"] | "";
        const uint32_t credential_version = complete_document["credential_version"] | 0;
        if (error || completed_protocol != 1 || credential_version == 0 ||
            public_id.isEmpty() || device_key.isEmpty())
        {
            device_key = "";
            SetFailure(NetDeviceBindingFailure::InvalidServerResponse);
            Serial.println("[设备绑定] 永久凭据响应格式无效。 ");
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        /*
         * 显式任务可能来自服务器重置后的恢复流程。只有服务器先接受新挑战并返回完整凭据，
         * 才在这里原子覆盖旧记录；此前设备始终保留原凭据，避免临时网络故障造成自锁。
         */
        const SysDeviceIdentityBindResult saved = SysDeviceIdentity_ReplaceBinding(
            public_id,
            device_key,
            credential_version);
        device_key = "";
        if (saved != SysDeviceIdentityBindResult::Ok)
        {
            SetFailure(NetDeviceBindingFailure::CredentialSaveFailed);
            Serial.printf("[设备绑定] 永久凭据落盘失败，结果=%u；未向服务端确认。\n",
                          static_cast<unsigned>(saved));
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        /* 服务端把首次使用新永久通行码成功登录视为落盘确认；响应若丢失则不会走到这里。 */
        const NetTaskExecutionResult authenticated = EnsureAuthentication(context);
        if (authenticated.disposition != NetTaskDisposition::Complete)
            return authenticated;

        Serial.printf("[设备绑定] 身份绑定完成，公开身份=%s；永久通行码未写入日志。\n",
                      public_id.c_str());
        SetFailure(NetDeviceBindingFailure::None);
        return {NetTaskDisposition::Complete, 0};
    }

    /** 已绑定设备必须先用当前短期会话证明自身，再让服务端开放一次新绑定。 */
    NetTaskExecutionResult ExecuteRebind(
        const NetTaskInvocation &invocation,
        const NetTaskContext &context)
    {
        SetFailure(NetDeviceBindingFailure::Unknown);
        const NetTaskExecutionResult authenticated = EnsureAuthentication(context);
        if (authenticated.disposition != NetTaskDisposition::Complete)
            return authenticated;

        NetHttpResponse response;
        const NetHttpResult transport = NetAuthSession_PostJson(
            "/api/v1/device/binding/rebind",
            "{}",
            context,
            kBindingResponseLimit,
            response);
        if (transport != NetHttpResult::Ok)
            return TemporaryFailure("申请重新绑定", transport, response);
        if (response.status_code == 403)
        {
            NetAuthSession_Clear();
            SetFailure(NetDeviceBindingFailure::DeviceDisabled);
            Serial.println("[设备绑定] 服务器已禁用此设备，拒绝自主重新绑定。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (response.status_code == 401)
        {
            NetAuthSession_Clear();
            SetFailure(NetDeviceBindingFailure::RebindAuthorizationFailed);
            Serial.println("[设备绑定] 旧设备会话已失效，无法授权自主重新绑定。 ");
            return {NetTaskDisposition::AuthBlocked, 0};
        }
        if (response.status_code != 200)
        {
            SetFailure(response.status_code >= 500
                           ? NetDeviceBindingFailure::ServerUnavailable
                           : NetDeviceBindingFailure::ServerRejected);
            Serial.printf("[设备绑定] 申请重新绑定失败，HTTP=%d。\n", response.status_code);
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        JsonDocument document;
        const DeserializationError error = deserializeJson(document, response.body);
        const bool rebind_required = document["rebind_required"] | false;
        const uint32_t credential_version = document["credential_version"] | 0;
        if (error || !rebind_required || credential_version == 0)
        {
            SetFailure(NetDeviceBindingFailure::InvalidServerResponse);
            Serial.println("[设备绑定] 重新绑定授权响应格式无效。 ");
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        /* 服务端此时已撤销旧会话；清除 RAM Token 后进入与恢复绑定共用的挑战流程。 */
        NetAuthSession_Clear();
        Serial.printf("[设备绑定] 服务器已开放重新绑定，目标凭据版本=%lu。\n",
                      static_cast<unsigned long>(credential_version));
        return ExecuteBinding(invocation, context);
    }
}

NetDeviceBindingFailure NetDeviceBinding_GetLastFailure()
{
    portENTER_CRITICAL(&s_failureMux);
    const NetDeviceBindingFailure failure = s_lastFailure;
    portEXIT_CRITICAL(&s_failureMux);
    return failure;
}

bool NetDeviceBinding_Register()
{
    NetTaskDefinition binding;
    binding.task_id = NET_TASK_DEVICE_BINDING;
    binding.name = "设备身份绑定";
    binding.execute = ExecuteBinding;
    binding.trigger_mask = NET_TASK_TRIGGER_NONE;
    if (!NetTaskRegistry_Register(binding))
        return false;

    NetTaskDefinition rebind;
    rebind.task_id = NET_TASK_DEVICE_REBIND;
    rebind.name = "设备自主重新绑定";
    rebind.execute = ExecuteRebind;
    rebind.trigger_mask = NET_TASK_TRIGGER_NONE;
    return NetTaskRegistry_Register(rebind);
}
