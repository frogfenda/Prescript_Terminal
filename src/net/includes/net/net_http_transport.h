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

/** HTTPClient 负错误码经传输层归一化后的稳定原因，避免业务层依赖框架私有宏。 */
enum class NetHttpFailureDetail : uint8_t
{
    None,
    SecureConnectionFailed,
    ServerConnectionFailed,
    RequestSendFailed,
    ConnectionLost,
    InsufficientMemory,
    ReadTimeout,
    Unknown,
};

struct NetHttpResponse
{
    int status_code = 0;
    String body;
    /*
    HTTPClient 在拿不到 HTTP 状态码时返回负数；WiFiClientSecure 则保留更底层的
    mbedTLS 错误。二者只用于失败分类和串口诊断，绝不能承载响应正文或凭据。
    */
    int transport_error_code = 0;
    int secure_error_code = 0;
    NetHttpFailureDetail failure_detail = NetHttpFailureDetail::None;
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
