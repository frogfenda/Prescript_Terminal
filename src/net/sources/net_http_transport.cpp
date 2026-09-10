/*
【模块职责】统一执行 HTTPS 请求。所有连接都校验受信根，且只有声明 Content-Length 且不超限的响应才进入内存。
【资源边界】单次请求对象只在 Core 0 网络任务栈内存在；响应大小由业务调用方给出硬上限。
*/
#include "net/net_http_transport.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace
{
    constexpr char kServerBaseUrl[] = "https://test.prescript.cloud";
    constexpr uint32_t kMaximumRequestTimeoutMs = 8000;
    constexpr uint32_t kMinimumRequestBudgetMs = 1200;

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
        const uint32_t remaining_ms = context.RemainingMs();
        if (remaining_ms <= kMinimumRequestBudgetMs)
            return NetHttpResult::DeadlineExceeded;

        const uint32_t timeout_ms = remaining_ms - 500 < kMaximumRequestTimeoutMs
                                        ? remaining_ms - 500
                                        : kMaximumRequestTimeoutMs;
        WiFiClientSecure client;
        client.setCACert(kIsrgRootX1);
        client.setTimeout(timeout_ms);

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
