// 文件：src/sys/sys_network.cpp
#include "sys_network.h"
#include <WiFi.h>
#include <time.h>

#define WIFI_TIMEOUT_MS 15000
#define NTP_TIMEOUT_MS  10000

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
    // 【架构修复】：false代表不擦除底层记录的凭据，加快下次连接速度
    WiFi.disconnect(true, false); 
    WiFi.mode(WIFI_OFF);         
    net_state = NET_IDLE;
}

NetworkState Network_GetState() { return net_state; }

void Network_Update() {
    uint32_t now = millis();

    if (net_state == NET_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            Network_StartNTP();
        } else if (now - timeout_start > WIFI_TIMEOUT_MS) { 
            net_state = NET_FAIL;
            Network_Disconnect();
        }
    } 
    else if (net_state == NET_SYNCING_NTP) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) { 
            net_state = NET_SYNC_SUCCESS; 
        } else if (now - timeout_start > NTP_TIMEOUT_MS) {
            net_state = NET_CONNECTED;    
        }
    }
}