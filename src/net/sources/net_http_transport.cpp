/*
【模块职责】统一执行 HTTPS 请求。所有连接都校验受信根，且只有声明 Content-Length 且不超限的响应才进入内存。
【资源边界】单次请求对象只在 Core 0 网络任务栈内存在；响应大小由业务调用方给出硬上限。
*/
#include "net/net_http_transport.h"
#include "net/net_tls_memory.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace
{
    constexpr char kServerBaseUrl[] = "https://test.prescript.cloud";
    constexpr uint32_t kMaximumRequestTimeoutMs = 8000;
    constexpr uint32_t kMinimumRequestBudgetMs = 1200;

    class TlsMemoryRequestGuard
    {
    public:
        TlsMemoryRequestGuard()
        {
            NetTlsMemory_BeginRequest();
        }

        ~TlsMemoryRequestGuard()
        {
            /*
             * 本对象先于 WiFiClientSecure 创建，因而析构顺序在客户端之后；此时快照中的
             * current_bytes 应回落到请求前基线，可以同时发现第三方库遗留的 TLS 对象。
             */
            const NetTlsMemorySnapshot snapshot = NetTlsMemory_GetSnapshot();
            Serial.printf(
                "[网络/TLS内存] 请求结束：本次峰值=%u，当前占用=%u，预算=%u，失败=%s，失败申请=%u。\n",
                static_cast<unsigned>(snapshot.request_peak_bytes),
                static_cast<unsigned>(snapshot.current_bytes),
                static_cast<unsigned>(snapshot.budget_bytes),
                NetTlsMemory_DescribeFailure(snapshot.last_failure),
                static_cast<unsigned>(snapshot.last_failed_request_bytes));
        }
    };

    /** 将 HTTPClient 的稳定负错误码翻译成中文，避免业务日志只有无法解释的数字。 */
    const char *DescribeTransportError(int error)
    {
        switch (error)
        {
        case HTTPC_ERROR_CONNECTION_REFUSED:
            return "连接服务器失败";
        case HTTPC_ERROR_SEND_HEADER_FAILED:
            return "发送请求头失败";
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
            return "发送请求正文失败";
        case HTTPC_ERROR_NOT_CONNECTED:
            return "连接未建立";
        case HTTPC_ERROR_CONNECTION_LOST:
            return "连接中途断开";
        case HTTPC_ERROR_NO_STREAM:
            return "响应数据流不可用";
        case HTTPC_ERROR_NO_HTTP_SERVER:
            return "服务端未返回 HTTP 响应";
        case HTTPC_ERROR_TOO_LESS_RAM:
            return "可用内存不足";
        case HTTPC_ERROR_ENCODING:
            return "响应编码不支持";
        case HTTPC_ERROR_STREAM_WRITE:
            return "响应流写入失败";
        case HTTPC_ERROR_READ_TIMEOUT:
            return "读取响应超时";
        default:
            return "未知传输错误";
        }
    }

    NetHttpFailureDetail ClassifyTransportFailure(int transport_error, int secure_error)
    {
        /* 明确的 mbedTLS 错误优先，-1 同时可能代表 TCP 或握手超时，不能冒充证书错误。 */
        if (secure_error < -1)
            return NetHttpFailureDetail::SecureConnectionFailed;
        switch (transport_error)
        {
        case HTTPC_ERROR_READ_TIMEOUT:
            return NetHttpFailureDetail::ReadTimeout;
        case HTTPC_ERROR_CONNECTION_REFUSED:
        case HTTPC_ERROR_NOT_CONNECTED:
        case HTTPC_ERROR_NO_STREAM:
        case HTTPC_ERROR_NO_HTTP_SERVER:
            return NetHttpFailureDetail::ServerConnectionFailed;
        case HTTPC_ERROR_SEND_HEADER_FAILED:
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
        case HTTPC_ERROR_STREAM_WRITE:
            return NetHttpFailureDetail::RequestSendFailed;
        case HTTPC_ERROR_CONNECTION_LOST:
            return NetHttpFailureDetail::ConnectionLost;
        case HTTPC_ERROR_TOO_LESS_RAM:
            return NetHttpFailureDetail::InsufficientMemory;
        default:
            return NetHttpFailureDetail::Unknown;
        }
    }

    /* Let's Encrypt ISRG Root X1；服务器证书链必须最终落到该受信根。 */
    constexpr char kIsrgRootX1[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

    NetHttpResult Perform(
        const char *method,
        const String &url,
        const String &body,
        const String &bearer,
        const NetTaskContext &context,
        size_t response_limit,
        NetHttpResponse &response)
    {
        response.status_code = 0;
        response.body = "";
        response.transport_error_code = 0;
        response.secure_error_code = 0;
        response.failure_detail = NetHttpFailureDetail::None;
        const uint32_t remaining_ms = context.RemainingMs();
        if (remaining_ms <= kMinimumRequestBudgetMs)
            return NetHttpResult::DeadlineExceeded;

        const uint32_t timeout_ms = remaining_ms - 500 < kMaximumRequestTimeoutMs
                                        ? remaining_ms - 500
                                        : kMaximumRequestTimeoutMs;
        const NetTlsMemorySnapshot memory = NetTlsMemory_GetSnapshot();
        if (!memory.installed)
        {
            Serial.println("[网络/TLS内存] 请求被拒绝：PSRAM分配器尚未安装。 ");
            return NetHttpResult::TlsInitializationFailed;
        }

        TlsMemoryRequestGuard memoryGuard;
        WiFiClientSecure client;
        client.setCACert(kIsrgRootX1);
        /*
        WiFiClientSecure::setTimeout() 的参数单位是秒，不能直接传 timeout_ms。
        HTTPClient 会在连接后设置读写超时；这里单独限制 TLS 握手，确保 Core 0
        不会被框架默认的 120 秒握手时限拖过当前网络任务截止时间。
        */
        client.setHandshakeTimeout((timeout_ms + 999UL) / 1000UL);

        HTTPClient http;
        http.setConnectTimeout(static_cast<int32_t>(timeout_ms));
        http.setTimeout(static_cast<uint16_t>(timeout_ms));
        if (!http.begin(client, url))
            return NetHttpResult::TlsInitializationFailed;

        http.addHeader("Accept", "application/json");
        if (!bearer.isEmpty())
            http.addHeader("Authorization", String("Bearer ") + bearer);

        int status_code = 0;
        if (strcmp(method, "POST") == 0)
        {
            http.addHeader("Content-Type", "application/json");
            status_code = http.POST(body);
        }
        else
        {
            status_code = http.GET();
        }

        if (status_code <= 0)
        {
            response.transport_error_code = status_code;
            char secure_error_text[128] = {};
            const int raw_secure_error = client.lastError(
                secure_error_text,
                sizeof(secure_error_text));
            /* 成功连接时该字段可能保存正的 socket 描述符，只有负数才是底层错误。 */
            response.secure_error_code = raw_secure_error < 0 ? raw_secure_error : 0;
            response.failure_detail = ClassifyTransportFailure(
                response.transport_error_code,
                response.secure_error_code);

            Serial.printf(
                "[网络/HTTPS] 请求失败：传输错误=%d（%s），TLS错误=%d，内部堆=%u，最大内部块=%u，PSRAM=%u，最大PSRAM块=%u。\n",
                status_code,
                DescribeTransportError(status_code),
                response.secure_error_code,
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
            if (response.secure_error_code < 0 && secure_error_text[0] != '\0')
            {
                Serial.printf("[网络/HTTPS] TLS底层说明：%s。\n", secure_error_text);
            }
            http.end();
            return NetHttpResult::TransportFailed;
        }

        response.status_code = status_code;
        const int content_length = http.getSize();
        if (content_length < 0 || static_cast<size_t>(content_length) > response_limit)
        {
            http.end();
            return NetHttpResult::ResponseTooLarge;
        }
        if (content_length > 0)
        {
            response.body.reserve(static_cast<size_t>(content_length) + 1);
            response.body = http.getString();
            if (response.body.length() != static_cast<size_t>(content_length) ||
                response.body.length() > response_limit)
            {
                response.body = "";
                http.end();
                return NetHttpResult::TransportFailed;
            }
        }
        http.end();
        return NetHttpResult::Ok;
    }
}

NetHttpResult NetHttp_PostJson(
    const char *path,
    const String &body,
    const String &bearer,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response)
{
    if (!path || path[0] != '/')
        return NetHttpResult::TlsInitializationFailed;
    return Perform("POST", String(kServerBaseUrl) + path, body, bearer, context, response_limit, response);
}

NetHttpResult NetHttp_GetJsonUrl(
    const char *url,
    const NetTaskContext &context,
    size_t response_limit,
    NetHttpResponse &response)
{
    if (!url || strncmp(url, "https://", 8) != 0)
        return NetHttpResult::TlsInitializationFailed;
    return Perform("GET", String(url), String(), String(), context, response_limit, response);
}
