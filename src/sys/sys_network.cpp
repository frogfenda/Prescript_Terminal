#include "sys_network.h"
#include <WiFi.h>
#include <time.h>

static NetworkState net_state = NET_IDLE;
static uint32_t timeout_start = 0;

void Network_Connect(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    net_state = NET_CONNECTING;
    timeout_start = millis();
}

void Network_StartNTP() {
    // 强制使用东八区配置 NTP
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    net_state = NET_SYNCING_NTP;
    timeout_start = millis();
}

void Network_Disconnect() {
    WiFi.disconnect(true);
    net_state = NET_IDLE;
}

NetworkState Network_GetState() { return net_state; }

// 非阻塞的核心驱动器
void Network_Update() {
    uint32_t now = millis();

    if (net_state == NET_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            net_state = NET_CONNECTED;
        } else if (now - timeout_start > 10000) { // 10秒超时
            net_state = NET_FAIL;
            Network_Disconnect();
        }
    } 
    else if (net_state == NET_SYNCING_NTP) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) { // 0延时非阻塞探测时间
            net_state = NET_SYNC_SUCCESS;
            Network_Disconnect(); // 获取成功后直接断网，极限省电！
        } else if (now - timeout_start > 10000) {
            net_state = NET_FAIL;
            Network_Disconnect();
        }
    }
}