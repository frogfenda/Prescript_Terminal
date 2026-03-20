// 文件：src/sys/sys_network.cpp
#include "sys_network.h"
#include "sys_config.h"
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h> // 【新增】：处理 HTTPS 必须引入的库
#include "sys_router.h"

// ==========================================
// 【隐秘炸弹】跨线程通信弹匣 (支持多发连射)
// ==========================================
volatile bool g_api_fetch_done = false;
volatile bool g_api_fetch_success = false;

// 定义一颗“指令子弹”
struct TempApiCmd {
    uint32_t tt;
    String tl;
    String ps;
};
TempApiCmd g_api_cmd_mag[5]; // 弹匣容量：一次最多拉取 5 条隐秘指令
volatile int g_api_cmd_count = 0;

// 【幽灵间谍线程】：专门负责在后台忍受网络延迟
void api_fetch_bg_task(void *pvParameters) {
    
    // ==========================================
    // 【核心修复】：彻底抛弃 SSL，改用轻量级普通客户端！
    // ==========================================
    WiFiClient client; // 【极其重要】：去掉了 Secure！不再需要 setInsecure()！
    
    HTTPClient http;

    // 使用明文 HTTP 协议，速度极快，内存占用几乎为零！
    http.begin(client, "http://index.dimension-404.cloud/api/schedule/sync"); 
    http.setTimeout(4000); 
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String payload = http.getString();
        
        JsonDocument doc; 
        if (!deserializeJson(doc, payload)) {
           JsonArray arr = doc.as<JsonArray>();
            int count = 0;
            for (JsonObject obj : arr) {
                if (count >= 5) break; 
                
                // ==========================================
                // 【核心修复】：精准匹配你全新的 API 键名！
                // ==========================================
                g_api_cmd_mag[count].tt = obj["time"] | 0;         // 从 "tt" 改为 "time"
                g_api_cmd_mag[count].tl = obj["title"] | "隐秘行动"; // 从 "tl" 改为 "title"
                g_api_cmd_mag[count].ps = obj["ps"] | "";          // 这个没变，保持原样
                
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

// 【新增】：后台静默对时专用的标记
bool g_bg_sync_active = false;
bool g_bg_was_connected = false;

void Network_Init()
{
    WiFi.mode(WIFI_OFF);
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

// 【新增】：强制重置NTP状态（用于已联网时重新对时）
void Network_ForceNTP()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        g_state = NET_CONNECTED; // 状态回退，触发下一步 configTime
        g_is_connecting = true;  // 激活状态机轮询
    }
}

// 【新增】：开机自动触发后台对时任务
void Network_AutoSyncTask()
{
    if (sysConfig.wifi_ssid.isEmpty())
        return;

    if (g_state == NET_DISCONNECTED || g_state == NET_CONNECT_FAILED)
    {
        g_bg_was_connected = false; // 记录：本来没网，是我临时打开的
        g_bg_sync_active = true;
        Network_Connect();
    }
    else
    {
        g_bg_was_connected = true; // 记录：本来就有网，我只借用一下
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

            // 后台静默任务失败，且本来没网，就关掉WiFi省电
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
        // 【防卡顿机制】：0毫秒非阻塞查询，绝对不卡主线程UI动画！
        if (getLocalTime(&timeinfo, 0))
        {
            g_state = NET_FETCHING_API;
            g_api_fetch_done = false;
            g_api_fetch_success = false;

            // 启动后台间谍线程拉取 API (分配 10KB 内存防 HTTPS 爆栈)
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
        // 幽灵线程下载完毕，回到主线程接应
        if (g_api_fetch_done)
        {
            g_state = NET_SYNC_SUCCESS;
            
            // 如果成功截获了指令阵列
            if (g_api_fetch_success && g_api_cmd_count > 0)
            {
                Serial.printf("[API] 成功截获 %d 条隐秘指令！\n", g_api_cmd_count);
                
                // 【第二刀解耦机制】：网络模块不再亲自写硬盘，而是把数据抛给路由中心！
                for (int i = 0; i < g_api_cmd_count; i++)
                {
                    if (g_api_cmd_mag[i].tt > 0)
                    {
                        SysRouter_ProcessAPI(g_api_cmd_mag[i].tt, 
                                             g_api_cmd_mag[i].tl, 
                                             g_api_cmd_mag[i].ps);
                        delay(50); // 给一点点时间让 LittleFS 写入硬盘，防卡顿
                    }
                }
            }

            // 间谍任务彻底结束，执行“拔网线”撤退逻辑，极限省电！
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
        // 如果是正常长期联网，到达成功后停止轮询，保持连接
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