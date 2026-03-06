// 文件：src/sys/sys_network.cpp
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
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    net_state = NET_SYNCING_NTP;
    timeout_start = millis();
}

void Network_Disconnect() {
    WiFi.disconnect(true, true); 
    WiFi.mode(WIFI_OFF);         
    net_state = NET_IDLE;
}

NetworkState Network_GetState() { return net_state; }

void Network_Update() {
    uint32_t now = millis();

    if (net_state == NET_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            // 【核心修改】：连上WiFi后，后台引擎直接自动发起 NTP 时间同步！
            Network_StartNTP();
        } else if (now - timeout_start > 15000) { 
            net_state = NET_FAIL;
            Network_Disconnect();
        }
    } 
    else if (net_state == NET_SYNCING_NTP) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) { 
            net_state = NET_SYNC_SUCCESS; // 同步成功，默默待命
        } else if (now - timeout_start > 10000) {
            net_state = NET_CONNECTED;    // 对时失败退回已连接状态
        }
    }
}