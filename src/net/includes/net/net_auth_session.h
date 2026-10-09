/*
【模块职责】用账号认领后写入 identity 分区的机器码与设备长期凭据换取短期 Bearer Token，并只在 RAM 中维护本轮会话。
【安全边界】业务模块不直接读取 Token；只允许通过本模块发送带认证头的 HTTPS JSON。
*/
#pragma once

#include <Arduino.h>
#include "net/net_http_transport.h"

enum class NetAuthEnsureResult : uint8_t
{
    Ready,
    DeviceUnbound,
    CredentialsRejected,
    TemporaryFailure,
};

/** 在新网络会话开始或 WiFi 关闭时清除 RAM Token。 */
void NetAuthSession_Clear();

/** 如本轮尚无 Token，则使用永久凭据创建服务端设备会话。 */
NetAuthEnsureResult NetAuthSession_Ensure(const NetTaskContext &context);

/**
 * 发送设备授权 JSON；Token 不会返回给业务调用方。普通 401 最多换取一次新 Token 并重发；
 * 解绑、禁用或凭据错误不重发，传输失败也不重发。只能在网络工作线程调用。
 */
NetHttpResult NetAuthSession_PostJson(
    const char *path,
    const String &body,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response);

/** 发送设备授权 GET；与 JSON 写请求共用 Token 失效处理和最多一次的认证重试。 */
NetHttpResult NetAuthSession_GetJson(
    const char *path,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response);
