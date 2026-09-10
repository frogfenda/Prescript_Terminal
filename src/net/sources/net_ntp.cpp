// 文件：src/net/sources/net_ntp.cpp
/*
【设计决定】NTP 是每次真实联网会话的基础阶段，不是普通业务任务。
它为 Outbox 的 not_before_epoch、重试退避和后续 TLS 校验建立可信时间基准。
*/
#include "net/net_ntp.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>
#include <esp_system.h>
#include "sys/sys_time.h"

namespace
{
    constexpr uint16_t kServerPort = 123;
    constexpr uint32_t kResponseTimeoutMs = 3200;
    constexpr uint64_t kUnixEpochOffset = 2208988800ULL;
    constexpr size_t kPacketSize = 48;
    const char *const kServers[] = {
        "pool.ntp.org",
        "time.nist.gov",
        "ntp1.aliyun.com",
    };
}

bool NetNtp_Sync(time_t &out_network_epoch, NetNtpAbortCheck is_abort_requested)
{
    out_network_epoch = 0;
    Serial.println("[网络/NTP] WiFi 已连接，开始基础校时。 ");

    WiFiUDP udp;
    const uint16_t local_port = static_cast<uint16_t>(49152U + (esp_random() & 0x3FFFU));
    if (!udp.begin(local_port))
    {
        Serial.println("[网络/NTP] UDP 本地端口创建失败。 ");
        return false;
    }

    for (const char *server_name : kServers)
    {
        if (is_abort_requested && is_abort_requested())
            break;

        IPAddress server_ip;
        if (WiFi.hostByName(server_name, server_ip) != 1)
        {
            Serial.printf("[网络/NTP] 域名解析失败：%s。\n", server_name);
            continue;
        }

        uint8_t request[kPacketSize] = {};
        request[0] = 0x23; // LI=0、VN=4、Mode=3（客户端）。

        /* 随机挑战值由服务端回显，防止误接收上一轮残留的 UDP 响应。 */
        const uint32_t nonce_high = esp_random();
        const uint32_t nonce_low = esp_random();
        request[40] = static_cast<uint8_t>(nonce_high >> 24);
        request[41] = static_cast<uint8_t>(nonce_high >> 16);
        request[42] = static_cast<uint8_t>(nonce_high >> 8);
        request[43] = static_cast<uint8_t>(nonce_high);
        request[44] = static_cast<uint8_t>(nonce_low >> 24);
        request[45] = static_cast<uint8_t>(nonce_low >> 16);
        request[46] = static_cast<uint8_t>(nonce_low >> 8);
        request[47] = static_cast<uint8_t>(nonce_low);

        while (udp.parsePacket() > 0)
            udp.flush();

        if (!udp.beginPacket(server_ip, kServerPort) ||
            udp.write(request, sizeof(request)) != sizeof(request) ||
            !udp.endPacket())
        {
            Serial.printf("[网络/NTP] 请求发送失败：%s。\n", server_name);
            continue;
        }

        const uint32_t wait_started_ms = millis();
        while (static_cast<uint32_t>(millis() - wait_started_ms) < kResponseTimeoutMs)
        {
            if (is_abort_requested && is_abort_requested())
            {
                udp.stop();
                return false;
            }

            const int packet_size = udp.parsePacket();
            if (packet_size < static_cast<int>(kPacketSize))
            {
                if (packet_size > 0)
                    udp.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            uint8_t response[kPacketSize] = {};
            const int read_length = udp.read(response, sizeof(response));
            const IPAddress response_ip = udp.remoteIP();
            const uint16_t response_port = udp.remotePort();
            udp.flush();
            if (read_length != static_cast<int>(sizeof(response)) ||
                response_ip != server_ip ||
                response_port != kServerPort)
                continue;

            const uint8_t leap_indicator = response[0] >> 6;
            const uint8_t mode = response[0] & 0x07;
            const uint8_t stratum = response[1];
            if (leap_indicator == 3 || mode != 4 || stratum == 0 || stratum > 15 ||
                memcmp(&response[24], &request[40], 8) != 0)
                continue;

            const uint32_t seconds32 =
                (static_cast<uint32_t>(response[40]) << 24) |
                (static_cast<uint32_t>(response[41]) << 16) |
                (static_cast<uint32_t>(response[42]) << 8) |
                static_cast<uint32_t>(response[43]);

            /* NTP 32 位秒数在 2036 年回绕，小于 1900→1970 偏移时按 era 1 解释。 */
            uint64_t ntp_seconds = seconds32;
            if (ntp_seconds < kUnixEpochOffset)
                ntp_seconds += (1ULL << 32);
            const time_t network_epoch = static_cast<time_t>(ntp_seconds - kUnixEpochOffset);

            if (!SysTime_SubmitNetworkTime(network_epoch))
            {
                Serial.println("[网络/NTP] UTC 无法投递给主循环。 ");
                udp.stop();
                return false;
            }

            /*
             * NTP epoch 只是网络线程取得的候选值。必须等待主循环完成 settimeofday，后续
             * WiFiClientSecure 才能用真实系统时间校验证书有效期；RTC 写回仍留在主循环。
             */
            if (!SysTime_WaitForNetworkTimeApplied(2000))
            {
                Serial.println("[网络/NTP] 等待系统时间应用超时，本轮拒绝继续执行 TLS 任务。 ");
                udp.stop();
                return false;
            }

            out_network_epoch = network_epoch;
            Serial.printf("[网络/NTP] 已取得可信 UTC：服务器=%s。\n", server_name);
            udp.stop();
            return true;
        }

        Serial.printf("[网络/NTP] 服务器响应超时：%s。\n", server_name);
    }

    udp.stop();
    return false;
}
