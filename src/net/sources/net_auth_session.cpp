/*
【模块职责】设备会话认证实现。账号认领后得到的长期设备凭据只在创建会话的局部变量中短暂存在，Token 仅保留到 WiFi 会话结束。
【失败语义】401/403 表示已存凭据被服务端拒绝；网络错误和 5xx 留待后续会话重试。
*/
#include "net/net_auth_session.h"

#include <ArduinoJson.h>
#include <cstring>
#include "sys/sys_device_identity.h"

namespace
{
    constexpr size_t kSessionResponseLimit = 768;
    String s_accessToken;

    void ClearSensitiveString(String &value)
    {
        for (size_t index = 0; index < value.length(); ++index)
            value.setCharAt(index, '\0');
        value = "";
    }

    /**
     * 业务请求与会话签发使用相同的稳定错误码。解绑必须撤销 RAM Token 和本地长期凭据；
     * 禁用、凭据错误及普通 Token 过期只撤销 RAM Token，不能将网络故障误当作解绑。
     * 本模块固定由网络工作线程调用；错误正文和 Token 均不进入串口。
     */
    bool InvalidateRejectedSession(const NetHttpResponse &response)
    {
        const bool unbound = response.device_error_code == "DEVICE_UNBOUND";
        const bool disabled = response.device_error_code == "DEVICE_DISABLED";
        const bool invalid = response.device_error_code == "INVALID_DEVICE_CREDENTIALS";
        if (response.status_code != 401 && response.status_code != 403 && !unbound && !disabled && !invalid)
            return false;
        NetAuthSession_Clear();
        if (unbound)
        {
            if (!SysDeviceIdentity_ClearBinding())
                Serial.println("[设备认证] 服务端已解绑，本地长期凭据暂未能清除，将在下轮重试。");
        }
        /* 仅没有长期凭据错误码的 401 才允许重新换取 Token；403 不自动越过权限拒绝。 */
        return response.status_code == 401 && !unbound && !disabled && !invalid;
    }

    /**
     * 统一发送设备授权请求。服务端在业务执行前拒绝过期 Token 时，在剩余会话预算内
     * 最多重新认证并重发一次；传输失败不会重发 POST，避免未知执行结果造成重复写入。
     */
    NetHttpResult SendAuthenticated(
        const char *path, const String *body, const NetTaskContext &context,
        size_t response_limit, NetHttpResponse &response)
    {
        if (s_accessToken.isEmpty())
            return NetHttpResult::TransportFailed;
        auto send = [&]() {
            return body ? NetHttp_PostJson(path, *body, s_accessToken, context, response_limit, response)
                        : NetHttp_GetJson(path, s_accessToken, context, response_limit, response);
        };
        NetHttpResult result = send();
        if (result != NetHttpResult::Ok || !InvalidateRejectedSession(response))
            return result;
        if (!context.CanStart(1500))
            return result;
        const NetAuthEnsureResult auth = NetAuthSession_Ensure(context);
        if (auth == NetAuthEnsureResult::TemporaryFailure)
            return NetHttpResult::TransportFailed;
        if (auth != NetAuthEnsureResult::Ready)
            return result;
        result = send();
        if (result == NetHttpResult::Ok)
            (void)InvalidateRejectedSession(response);
        return result;
    }
}

void NetAuthSession_Clear()
{
    ClearSensitiveString(s_accessToken);
}

NetAuthEnsureResult NetAuthSession_Ensure(const NetTaskContext &context)
{
    if (!s_accessToken.isEmpty())
        return NetAuthEnsureResult::Ready;

    String machine_code;
    String device_key;
    if (!SysDeviceIdentity_CopyAuthCredentials(machine_code, device_key))
        return NetAuthEnsureResult::DeviceUnbound;

    JsonDocument request_document;
    request_document["machine_code"] = machine_code;
    request_document["device_key"] = device_key;
    String request_body;
    serializeJson(request_document, request_body);
    request_document.clear();
    ClearSensitiveString(device_key);

    NetHttpResponse response;
    const NetHttpResult transport = NetHttp_PostJson(
        "/api/v1/auth/devices/session",
        request_body,
        String(),
        context,
        kSessionResponseLimit,
        response);
    ClearSensitiveString(request_body);
    if (transport != NetHttpResult::Ok)
    {
        Serial.printf("[设备认证] 创建临时会话失败，传输结果=%u。\n",
                      static_cast<unsigned>(transport));
        return NetAuthEnsureResult::TemporaryFailure;
    }
    if (response.device_error_code == "DEVICE_UNBOUND")
    {
        (void)InvalidateRejectedSession(response);
        Serial.println("[设备认证] 服务端确认设备已解绑，需要重新进入账号认领流程。");
        return NetAuthEnsureResult::DeviceUnbound;
    }
    if (response.device_error_code == "DEVICE_DISABLED")
    {
        Serial.println("[设备认证] 服务端已禁用设备，暂停自动认证。 ");
        return NetAuthEnsureResult::CredentialsRejected;
    }
    if (response.status_code == 401 || response.status_code == 403 ||
        response.device_error_code == "INVALID_DEVICE_CREDENTIALS")
    {
        Serial.println("[设备认证] 服务端拒绝已存设备凭据。 ");
        return NetAuthEnsureResult::CredentialsRejected;
    }
    if (response.status_code != 200)
    {
        Serial.printf("[设备认证] 创建临时会话失败，HTTP=%d。\n", response.status_code);
        return NetAuthEnsureResult::TemporaryFailure;
    }

    JsonDocument response_document;
    const DeserializationError error = deserializeJson(response_document, response.body);
    const char *token = response_document["access_token"] | "";
    const char *token_type = response_document["token_type"] | "";
    const char *expires_at = response_document["expires_at"] | "";
    if (error || strlen(token) < 32 || strlen(token) > 128 ||
        strcmp(token_type, "bearer") != 0 || expires_at[0] == '\0')
    {
        Serial.println("[设备认证] 临时会话响应格式无效。 ");
        return NetAuthEnsureResult::TemporaryFailure;
    }

    s_accessToken = token;
    response_document.clear();
    ClearSensitiveString(response.body);
    return NetAuthEnsureResult::Ready;
}

NetHttpResult NetAuthSession_PostJson(
    const char *path,
    const String &body,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response)
{
    return SendAuthenticated(path, &body, context, response_limit, response);
}

NetHttpResult NetAuthSession_GetJson(
    const char *path,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response)
{
    return SendAuthenticated(path, nullptr, context, response_limit, response);
}
