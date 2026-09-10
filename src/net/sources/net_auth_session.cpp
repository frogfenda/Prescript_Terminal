/*
【模块职责】设备会话认证实现。永久通行码只在创建会话的局部变量中短暂存在，Token 仅保留到 WiFi 会话结束。
【失败语义】401/403 表示已存凭据被服务端拒绝；网络错误和 5xx 留待后续会话重试。
*/
#include "net/net_auth_session.h"

#include <ArduinoJson.h>
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
    ClearSensitiveString(device_key);

    NetHttpResponse response;
    const NetHttpResult transport = NetHttp_PostJson(
        "/api/v1/device/session",
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
    if (response.status_code == 401 || response.status_code == 403)
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
    if (error || strlen(token) < 32 || strlen(token) > 128)
    {
        Serial.println("[设备认证] 临时会话响应格式无效。 ");
        return NetAuthEnsureResult::TemporaryFailure;
    }

    s_accessToken = token;
    Serial.println("[设备认证] 已建立本轮临时会话；Token 未写入日志。 ");
    return NetAuthEnsureResult::Ready;
}

NetHttpResult NetAuthSession_PostJson(
    const char *path,
    const String &body,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response)
{
    if (s_accessToken.isEmpty())
        return NetHttpResult::TransportFailed;
    return NetHttp_PostJson(path, body, s_accessToken, context, response_limit, response);
}
