/*
【模块职责】提供受会话预算约束的 HTTPS JSON 传输，并集中维护服务器地址、根证书和响应上限。
【安全边界】禁止关闭证书校验；请求正文、永久通行码和 Bearer Token 均不得写入串口日志。
*/
#pragma once

#include <Arduino.h>
#include "net/net_task_registry.h"

enum class NetHttpResult : uint8_t
{
    Ok,
    DeadlineExceeded,
    TlsInitializationFailed,
    TransportFailed,
    ResponseTooLarge,
};

struct NetHttpResponse
{
    int status_code = 0;
    String body;
};

/** 向主服务端相对路径发送 JSON；bearer 为空时不附带 Authorization。 */
NetHttpResult NetHttp_PostJson(
    const char *path,
    const String &body,
    const String &bearer,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response);

/** 从完整 HTTPS URL 获取 JSON，供仍在迁移中的只读旧服务使用。 */
NetHttpResult NetHttp_GetJsonUrl(
    const char *url,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response);
