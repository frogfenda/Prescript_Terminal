/*
【模块职责】统一执行 HTTPS 请求。所有连接都校验受信根，且只有声明 Content-Length 且不超限的响应才进入内存。
【资源边界】单次请求对象只在 Core 0 网络任务栈内存在；响应大小由业务调用方给出硬上限。
*/
#include "net/net_http_transport.h"
#include "net/net_tls_memory.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace
{
    constexpr char kServerBaseUrl[] = "https://test.prescript.cloud";
    constexpr uint32_t kMaximumRequestTimeoutMs = 8000;
    constexpr uint32_t kMinimumRequestBudgetMs = 1200;
    constexpr uint32_t kConnectionRetryDelayMs = 350;
    constexpr uint8_t kMaximumAttempts = 2;

    struct HttpsEndpoint
    {
        String host;
        uint16_t port = 443;
    };

    /**
     * 从已经通过 HTTPS 前缀检查的 URL 中提取主机和端口。
     * 传输层目前只使用普通域名，不接受 URL 用户信息或 IPv6 字面量，避免诊断解析器
     * 与 HTTPClient 对同一个地址产生不同理解。
     */
    bool ParseHttpsEndpoint(const String &url, HttpsEndpoint &endpoint)
    {
        if (!url.startsWith("https://"))
            return false;

        const int authority_start = 8;
        int authority_end = url.indexOf('/', authority_start);
        if (authority_end < 0)
            authority_end = url.length();
        String authority = url.substring(authority_start, authority_end);
        if (authority.isEmpty() || authority.indexOf('@') >= 0 || authority.indexOf('[') >= 0)
            return false;

        const int colon = authority.lastIndexOf(':');
        if (colon >= 0)
        {
            const long parsed_port = authority.substring(colon + 1).toInt();
            if (parsed_port <= 0 || parsed_port > 65535)
                return false;
            endpoint.port = static_cast<uint16_t>(parsed_port);
            authority.remove(colon);
        }
        if (authority.isEmpty())
            return false;

        endpoint.host = authority;
        return true;
    }

    /** 只有明确发生在连接建立前后的框架错误才允许自动重试 POST。 */
    bool IsConnectionStageFailure(int transport_error)
    {
        /*
         * HTTPClient 只在 connect()（含 TCP 与 TLS）尚未完成时返回 -1。
         * -4/-7 可能发生在请求已发出、等待响应期间，自动重发 POST 会有重复副作用，不能重试。
         */
        return transport_error == HTTPC_ERROR_CONNECTION_REFUSED;
    }

    /**
     * 输出一次低频网络快照。这里只记录链路元数据，绝不记录 URL 路径、正文、Token 或永久 Key。
     * DNS 结果由本轮真实解析取得，便于把域名故障与后续 TCP/TLS 故障分开。
     */
    void PrintNetworkSnapshot(
        uint8_t attempt,
        const HttpsEndpoint &endpoint,
        bool dns_ok,
        const IPAddress &server_ip)
    {
        const wl_status_t wifi_status = WiFi.status();
        const String local_ip = WiFi.localIP().toString();
        const String resolved_ip = dns_ok ? server_ip.toString() : String("未解析");
        const int32_t rssi = wifi_status == WL_CONNECTED ? WiFi.RSSI() : 0;
        Serial.printf(
            "[网络/HTTPS] 请求前快照：尝试=%u，WiFi状态=%d，本机IP=%s，RSSI=%ld dBm，目标=%s:%u，DNS=%s。\n",
            static_cast<unsigned>(attempt),
            static_cast<int>(wifi_status),
            local_ip.c_str(),
            static_cast<long>(rssi),
            endpoint.host.c_str(),
            static_cast<unsigned>(endpoint.port),
            resolved_ip.c_str());
    }

    /**
     * 在同一请求的关键生命周期点输出内存快照。TLS 当前占用与本次峰值同时保留，
     * 可判断失败是否已经越过 TCP 建连并进入 mbedTLS 动态分配阶段。
     */
    void PrintMemorySnapshot(const char *stage, const NetTlsMemorySnapshot &tls_memory)
    {
        Serial.printf(
            "[网络/HTTPS] %s内存：内部堆=%u，最大内部块=%u，PSRAM=%u，最大PSRAM块=%u，TLS当前=%u，本次峰值=%u/%u。\n",
            stage,
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(tls_memory.current_bytes),
            static_cast<unsigned>(tls_memory.request_peak_bytes),
            static_cast<unsigned>(tls_memory.budget_bytes));
    }

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
        HttpsEndpoint endpoint;
        if (!ParseHttpsEndpoint(url, endpoint))
            return NetHttpResult::TlsInitializationFailed;

        const NetTlsMemorySnapshot memory = NetTlsMemory_GetSnapshot();
        if (!memory.installed)
        {
            Serial.println("[网络/TLS内存] 请求被拒绝：PSRAM分配器尚未安装。 ");
            return NetHttpResult::TlsInitializationFailed;
        }

        for (uint8_t attempt = 1; attempt <= kMaximumAttempts; ++attempt)
        {
            const uint32_t remaining_ms = context.RemainingMs();
            if (remaining_ms <= kMinimumRequestBudgetMs)
                return NetHttpResult::DeadlineExceeded;
            const uint32_t timeout_ms = remaining_ms - 500 < kMaximumRequestTimeoutMs
                                            ? remaining_ms - 500
                                            : kMaximumRequestTimeoutMs;

            if (WiFi.status() != WL_CONNECTED)
            {
                IPAddress empty_ip;
                PrintNetworkSnapshot(attempt, endpoint, false, empty_ip);
                response.transport_error_code = HTTPC_ERROR_NOT_CONNECTED;
                response.failure_detail = NetHttpFailureDetail::WifiUnavailable;
                Serial.println("[网络/HTTPS] 请求失败阶段：WiFi 已不在连接状态。 ");
                return NetHttpResult::TransportFailed;
            }

            IPAddress server_ip;
            const bool dns_ok = WiFi.hostByName(endpoint.host.c_str(), server_ip) == 1;
            PrintNetworkSnapshot(attempt, endpoint, dns_ok, server_ip);
            PrintMemorySnapshot("请求前", NetTlsMemory_GetSnapshot());
            if (!dns_ok)
            {
                response.transport_error_code = HTTPC_ERROR_CONNECTION_REFUSED;
                response.failure_detail = NetHttpFailureDetail::DnsResolutionFailed;
                Serial.println("[网络/HTTPS] 请求失败阶段：DNS 域名解析失败。 ");
                if (attempt < kMaximumAttempts && context.RemainingMs() > kMinimumRequestBudgetMs + kConnectionRetryDelayMs)
                {
                    Serial.printf("[网络/HTTPS] DNS 失败，%lu ms 后进行第 2 次尝试。\n",
                                  static_cast<unsigned long>(kConnectionRetryDelayMs));
                    vTaskDelay(pdMS_TO_TICKS(kConnectionRetryDelayMs));
                    continue;
                }
                return NetHttpResult::TransportFailed;
            }

            NetTlsMemory_BeginRequest();
            const size_t tls_bytes_before_attempt = NetTlsMemory_GetSnapshot().current_bytes;
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

            if (status_code > 0)
            {
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
                client.stop();
                vTaskDelay(pdMS_TO_TICKS(20));
                PrintMemorySnapshot("请求释放后", NetTlsMemory_GetSnapshot());
                if (attempt > 1)
                    Serial.printf("[网络/HTTPS] 第 %u 次尝试成功：HTTP=%d。\n",
                                  static_cast<unsigned>(attempt), status_code);
                return NetHttpResult::Ok;
            }

            response.transport_error_code = status_code;
            char secure_error_text[128] = {};
            const int raw_secure_error = client.lastError(secure_error_text, sizeof(secure_error_text));
            /* 成功连接时该字段可能保存正的 socket 描述符，只有负数才是底层错误。 */
            response.secure_error_code = raw_secure_error < 0 ? raw_secure_error : 0;
            response.failure_detail = ClassifyTransportFailure(
                response.transport_error_code,
                response.secure_error_code);

            Serial.printf("[网络/HTTPS] 请求失败：传输错误=%d（%s），TLS错误=%d。\n",
                          status_code,
                          DescribeTransportError(status_code),
                          response.secure_error_code);
            const NetTlsMemorySnapshot tls_memory = NetTlsMemory_GetSnapshot();
            PrintMemorySnapshot("失败时", tls_memory);
            if (tls_memory.last_failure != NetTlsMemoryFailure::None)
            {
                Serial.printf(
                    "[网络/TLS内存] 分配失败：原因=%s，申请=%u，本次峰值=%u，预算=%u。\n",
                    NetTlsMemory_DescribeFailure(tls_memory.last_failure),
                    static_cast<unsigned>(tls_memory.last_failed_request_bytes),
                    static_cast<unsigned>(tls_memory.request_peak_bytes),
                    static_cast<unsigned>(tls_memory.budget_bytes));
            }
            if (response.secure_error_code < 0 && secure_error_text[0] != '\0')
                Serial.printf("[网络/HTTPS] TLS底层说明：%s。\n", secure_error_text);

            /* 先完整释放失败连接，再判断阶段并退避，避免旧 socket 干扰下一次尝试。 */
            http.end();
            client.stop();
            vTaskDelay(pdMS_TO_TICKS(20));
            PrintMemorySnapshot("失败释放后", NetTlsMemory_GetSnapshot());

            const bool connection_stage_failure = IsConnectionStageFailure(status_code);
            if (connection_stage_failure)
            {
                const bool entered_tls_allocation =
                    tls_memory.request_peak_bytes > tls_bytes_before_attempt;
                if (entered_tls_allocation)
                {
                    Serial.println(
                        "[网络/HTTPS] 连接分段诊断：DNS=成功，TCP 已建立并进入 TLS；故障位于 TLS 握手或 HTTPS 建连阶段。 ");
                }
                else
                {
                    Serial.println(
                        "[网络/HTTPS] 连接分段诊断：DNS=成功，TLS 未产生动态分配；故障位于 socket/TCP 建连阶段。 ");
                }
                if (response.secure_error_code >= -1)
                {
                    response.failure_detail = entered_tls_allocation
                                                  ? NetHttpFailureDetail::SecureConnectionFailed
                                                  : NetHttpFailureDetail::TcpConnectionFailed;
                }

                if (attempt < kMaximumAttempts &&
                    context.RemainingMs() > kMinimumRequestBudgetMs + kConnectionRetryDelayMs)
                {
                    Serial.printf("[网络/HTTPS] 连接阶段失败，%lu ms 后进行第 2 次尝试。\n",
                                  static_cast<unsigned long>(kConnectionRetryDelayMs));
                    vTaskDelay(pdMS_TO_TICKS(kConnectionRetryDelayMs));
                    continue;
                }
            }
            return NetHttpResult::TransportFailed;
        }
        return NetHttpResult::TransportFailed;
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
