/*
【模块职责】用 identity 分区中的机器码与永久通行码换取短期 Bearer Token，并只在 RAM 中维护本轮会话。
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

/** 发送已认证 JSON；Token 不会返回给业务调用方。 */
NetHttpResult NetAuthSession_PostJson(
    const char *path,
    const String &body,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response);
