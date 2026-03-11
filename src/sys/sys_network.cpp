// 文件：src/sys/sys_network.cpp
#include "sys_network.h"
#include "sys_config.h"
#include <WiFi.h>
#include <time.h>

NetworkState g_state = NET_DISCONNECTED;
uint32_t g_connect_start_time = 0;
uint32_t g_ntp_start_time = 0;
bool g_is_connecting = false;

// 【新增】：后台静默对时专用的标记
bool g_bg_sync_active = false;
bool g_bg_was_connected = false;

void Network_Init() {
    WiFi.mode(WIFI_OFF);
}

void Network_Connect() {
    if (sysConfig.wifi_ssid.isEmpty()) return;
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());
    
    g_state = NET_CONNECTING;
    g_connect_start_time = millis();
    g_is_connecting = true;
}

void Network_Disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); // 彻底关闭射频，极限省电！
    g_state = NET_DISCONNECTED;
    g_is_connecting = false;
    g_bg_sync_active = false; 
}

// 【新增】：强制重置NTP状态（用于已联网时重新对时）
void Network_ForceNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        g_state = NET_CONNECTED; // 状态回退，触发下一步 configTime
        g_is_connecting = true;  // 激活状态机轮询
    }
}

// 【新增】：开机自动触发后台对时任务
void Network_AutoSyncTask() {
    if (sysConfig.wifi_ssid.isEmpty()) return;
    
    if (g_state == NET_DISCONNECTED || g_state == NET_CONNECT_FAILED) {
        g_bg_was_connected = false; // 记录：本来没网，是我临时打开的
        g_bg_sync_active = true;
        Network_Connect();
    } else {
        g_bg_was_connected = true;  // 记录：本来就有网，我只借用一下
        g_bg_sync_active = true;
        Network_ForceNTP();
    }
}

void Network_Update() {
    if (!g_is_connecting) return;

    if (g_state == NET_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            g_state = NET_CONNECTED;
        } else if (millis() - g_connect_start_time > 10000) { // 10秒超时
            g_state = NET_CONNECT_FAILED;
            g_is_connecting = false;
            
            // 后台静默任务失败，且本来没网，就关掉WiFi省电
            if (g_bg_sync_active && !g_bg_was_connected) {
                Network_Disconnect();
            }
            g_bg_sync_active = false;
        }
    } else if (g_state == NET_CONNECTED) {
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov", "ntp1.aliyun.com");
        g_state = NET_SYNCING_NTP;
        g_ntp_start_time = millis();
    } else if (g_state == NET_SYNCING_NTP) {
        struct tm timeinfo;
        // 快速尝试获取时间，10ms超时，非阻塞
        if (getLocalTime(&timeinfo, 10)) { 
            g_state = NET_SYNC_SUCCESS;
            
            // 后台静默任务成功
            if (g_bg_sync_active) {
                g_bg_sync_active = false;
                if (!g_bg_was_connected) {
                    Network_Disconnect(); // 只有是我打开的，我才负责关掉
                } else {
                    g_is_connecting = false; // 停止轮询
                }
            }
        } else if (millis() - g_ntp_start_time > 10000) { // 10秒对时超时
            g_state = NET_SYNC_FAILED;
            
            if (g_bg_sync_active) {
                g_bg_sync_active = false;
                if (!g_bg_was_connected) {
                    Network_Disconnect();
                } else {
                    g_is_connecting = false;
                }
            }
        }
    } else if (g_state == NET_SYNC_SUCCESS) {
        // 如果是正常长期联网，到达成功后停止轮询，保持连接
        if (!g_bg_sync_active) {
            g_is_connecting = false;
        }
    }
}

NetworkState Network_GetState() {
    return g_state;
}