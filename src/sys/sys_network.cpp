// 文件：src/sys/sys_network.cpp
#include "sys_network.h"
#include "sys_config.h"
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "sys_router.h"
#include "sys_event.h" // 【核心新增】：引入系统事件总线，准备收信

// ==========================================
// 【隐秘炸弹】跨线程通信弹匣 (支持多发连射)
// ==========================================
volatile bool g_api_fetch_done = false;
volatile bool g_api_fetch_success = false;

// 定义一颗“指令子弹”
struct TempApiCmd
{
    uint32_t tt;
    String tl;
    String ps;
};
TempApiCmd g_api_cmd_mag[5]; // 弹匣容量：一次最多拉取 5 条隐秘指令
volatile int g_api_cmd_count = 0;

// 【幽灵间谍线程】：专门负责在后台忍受网络延迟
void api_fetch_bg_task(void *pvParameters)
{

    WiFiClient client;
    HTTPClient http;

    // 使用明文 HTTP 协议，速度极快，内存占用几乎为零！
    http.begin(client, "http://index.dimension-404.cloud/api/schedule/sync");
    http.setTimeout(4000);

    int httpCode = http.GET();
    if (httpCode == 200)
    {
        String payload = http.getString();

        JsonDocument doc;
        if (!deserializeJson(doc, payload))
        {
            JsonArray arr = doc.as<JsonArray>();
            int count = 0;
            for (JsonObject obj : arr)
            {
                if (count >= 5)
                    break;

                g_api_cmd_mag[count].tt = obj["time"] | 0;
                g_api_cmd_mag[count].tl = obj["title"] | "隐秘行动";
                g_api_cmd_mag[count].ps = obj["ps"] | "";

                count++;
            }
            g_api_cmd_count = count;
            g_api_fetch_success = true;
        }
    }
    http.end();

    g_api_fetch_done = true;
    vTaskDelete(NULL);
}

NetworkState g_state = NET_DISCONNECTED;
uint32_t g_connect_start_time = 0;
uint32_t g_ntp_start_time = 0;
bool g_is_connecting = false;

bool g_bg_sync_active = false;
bool g_bg_was_connected = false;

// ==========================================
// 【核心解耦】：默默收信，悄悄存盘
// ==========================================
void _Cb_WifiSet(void *payload)
{
    Evt_WifiSet_t *p = (Evt_WifiSet_t *)payload;
    if (p && p->ssid)
    {
        // 直接覆盖配置并写入物理硬盘
        sysConfig.wifi_ssid = String(p->ssid);
        sysConfig.wifi_pass = String(p->pass);
        sysConfig.save();

        Serial.printf("[网络中枢] 已默默截获并保存新 WiFi 密钥: %s\n", p->ssid);
        // 【特性】：绝不弹窗打扰用户，深藏功与名！
    }
}

void Network_Init()
{
    WiFi.mode(WIFI_OFF);

    // 【点睛之笔】：在网络中心初始化时，向邮局订阅 WiFi 配置的电报！
    SysEvent_Subscribe(EVT_WIFI_SET, _Cb_WifiSet);
}

void Network_Connect()
{
    if (sysConfig.wifi_ssid.isEmpty())
        return;

    WiFi.mode(WIFI_STA);
    WiFi.begin(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());

    g_state = NET_CONNECTING;
    g_connect_start_time = millis();
    g_is_connecting = true;
}

void Network_Disconnect()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); // 彻底关闭射频，极限省电！
    g_state = NET_DISCONNECTED;
    g_is_connecting = false;
    g_bg_sync_active = false;
}

void Network_ForceNTP()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        g_state = NET_CONNECTED;
        g_is_connecting = true;
    }
}

void Network_AutoSyncTask()
{
    if (sysConfig.wifi_ssid.isEmpty())
        return;

    if (g_state == NET_DISCONNECTED || g_state == NET_CONNECT_FAILED)
    {
        g_bg_was_connected = false;
        g_bg_sync_active = true;
        Network_Connect();
    }
    else
    {
        g_bg_was_connected = true;
        g_bg_sync_active = true;
        Network_ForceNTP();
    }
}

void Network_Update()
{
    if (!g_is_connecting)
        return;

    if (g_state == NET_CONNECTING)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            g_state = NET_CONNECTED;
        }
        else if (millis() - g_connect_start_time > 10000)
        { // 10秒超时
            g_state = NET_CONNECT_FAILED;
            g_is_connecting = false;

            if (g_bg_sync_active && !g_bg_was_connected)
            {
                Network_Disconnect();
            }
            g_bg_sync_active = false;
        }
    }
    else if (g_state == NET_CONNECTED)
    {
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov", "ntp1.aliyun.com");
        g_state = NET_SYNCING_NTP;
        g_ntp_start_time = millis();
    }
    else if (g_state == NET_SYNCING_NTP)
    {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0))
        {
            g_state = NET_FETCHING_API;
            g_api_fetch_done = false;
            g_api_fetch_success = false;

            xTaskCreatePinnedToCore(api_fetch_bg_task, "API_Task", 10240, NULL, 1, NULL, 0);
        }
        else if (millis() - g_ntp_start_time > 10000)
        {
            g_state = NET_SYNC_FAILED;

            if (g_bg_sync_active)
            {
                g_bg_sync_active = false;
                if (!g_bg_was_connected)
                {
                    Network_Disconnect();
                }
                else
                {
                    g_is_connecting = false;
                }
            }
        }
    }
    else if (g_state == NET_FETCHING_API)
    {
        if (g_api_fetch_done)
        {
            g_state = NET_SYNC_SUCCESS;

            if (g_api_fetch_success && g_api_cmd_count > 0)
            {
                Serial.printf("[API] 成功截获 %d 条隐秘指令！\n", g_api_cmd_count);

                for (int i = 0; i < g_api_cmd_count; i++)
                {
                    if (g_api_cmd_mag[i].tt > 0)
                    {
                        SysRouter_ProcessAPI(g_api_cmd_mag[i].tt,
                                             g_api_cmd_mag[i].tl,
                                             g_api_cmd_mag[i].ps);
                        delay(50);
                    }
                }
            }

            if (g_bg_sync_active)
            {
                g_bg_sync_active = false;
                if (!g_bg_was_connected)
                {
                    Network_Disconnect();
                }
                else
                {
                    g_is_connecting = false;
                }
            }
        }
    }
    else if (g_state == NET_SYNC_SUCCESS)
    {
        if (!g_bg_sync_active)
        {
            g_is_connecting = false;
        }
    }
}

NetworkState Network_GetState()
{
    return g_state;
}