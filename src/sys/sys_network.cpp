// 文件：src/sys/sys_network.cpp
#include "sys_network.h"
#include "sys_config.h"
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "sys_router.h"
#include "sys_event.h"

volatile NetworkState g_state = NET_DISCONNECTED;
TaskHandle_t g_netTaskHandle = NULL;

void _Cb_WifiSet(void* payload) {
    Evt_WifiSet_t* p = (Evt_WifiSet_t*)payload;
    if (p && p->ssid) {
        sysConfig.wifi_ssid = String(p->ssid);
        sysConfig.wifi_pass = String(p->pass);
        sysConfig.save();
        Serial.printf("[网络中枢] 已截获新 WiFi 密钥: %s，立即在后台发起同步！\n", p->ssid);
        
        // 收到密码后，自动打响指触发后台同步！
        Network_StartSync();
    }
}

// ==========================================
// 【终极武器】：Core 0 纯后台网络守护神
// ==========================================
void network_daemon_task(void *pvParameters) {
    while (true) {
        // 1. 深度挂起：死等主线程发来的“启动信号” (不消耗任何 CPU)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (sysConfig.wifi_ssid.isEmpty()) {
            g_state = NET_CONNECT_FAILED;
            continue;
        }

        // 2. 第一关：连接 WiFi
        g_state = NET_CONNECTING;
        Serial.println("[网络守护神] 唤醒！开始物理拨号...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());

        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 20) { // 10秒超时 (20 * 500ms)
            vTaskDelay(pdMS_TO_TICKS(500)); 
            timeout++;
        }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[网络守护神] WiFi 拨号超时！");
            g_state = NET_CONNECT_FAILED;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            continue;
        }

        // 3. 第二关：NTP 对时 (此处 configTime 的 DNS 阻塞全被按在 Core 0，绝不卡 UI)
        Serial.println("[网络守护神] 拨号成功，开始解析 NTP 与对时...");
        g_state = NET_SYNCING_NTP;
        configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov", "ntp1.aliyun.com");

        timeout = 0;
        struct tm timeinfo;
        while (!getLocalTime(&timeinfo, 0) && timeout < 20) { // 10秒超时
            vTaskDelay(pdMS_TO_TICKS(500));
            timeout++;
        }

        if (timeout >= 20) {
            Serial.println("[网络守护神] NTP 对时彻底失败！");
            g_state = NET_SYNC_FAILED;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            continue;
        }

        // 4. 第三关：拉取 API 隐秘指令
        Serial.println("[网络守护神] NTP 校准完毕，开始截获隐秘指令...");
        g_state = NET_FETCHING_API;

        WiFiClient client;
        HTTPClient http;
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
                    uint32_t tt = obj["time"] | 0;
                    String tl = obj["title"] | "隐秘行动";
                    String ps = obj["ps"] | "";
                    if (tt > 0) {
                        SysRouter_ProcessAPI(tt, tl, ps);
                        vTaskDelay(pdMS_TO_TICKS(50)); // 慢条斯理地写硬盘
                    }
                    count++;
                }
                Serial.printf("[网络守护神] 成功截获并潜入 %d 条隐秘指令！\n", count);
            }
        } else {
            Serial.printf("[网络守护神] API 请求坠毁，HTTP 码: %d\n", httpCode);
        }
        http.end();

        // 5. 完美收工：通报全员，并拔掉网线省电！
        Serial.println("[网络守护神] 所有任务全线竣工！撤退并关闭射频...");
        g_state = NET_SYNC_SUCCESS;
        
        // 故意停留 2 秒，让 UI 界面的玩家能看到“同步成功”的文字，再物理断电
        vTaskDelay(pdMS_TO_TICKS(2000)); 
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

void Network_Init() {
    WiFi.mode(WIFI_OFF);
    SysEvent_Subscribe(EVT_WIFI_SET, _Cb_WifiSet);
    
    // 【点睛之笔】：开机就立刻把守护神按在 Core 0，分配大内存，挂起待命！
    xTaskCreatePinnedToCore(network_daemon_task, "NetDaemon", 10240, NULL, 1, &g_netTaskHandle, 0);
}

// 【系统级响指】：无论你在哪里，调用这句，守护神就会苏醒
void Network_StartSync() {
    // 只有在空闲状态才允许唤醒，防止重复发射
    if (g_netTaskHandle != NULL && 
        g_state != NET_CONNECTING && 
        g_state != NET_SYNCING_NTP && 
        g_state != NET_FETCHING_API) 
    {
        g_state = NET_CONNECTING; 
        xTaskNotifyGive(g_netTaskHandle); // 唤醒 Core 0 守护神！
    }
}

NetworkState Network_GetState() {
    return g_state;
}