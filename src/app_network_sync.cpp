#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h"   // 引入配置中心
#include "sys_network.h"  // 引入底层网络引擎

class AppNetworkSync : public AppBase {
private:
    uint32_t anim_time;
    int anim_dots;
    uint32_t result_show_time;
    bool is_finished;

    void drawUI(const char* base_text, bool show_dots = false) {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        
        char display_buf[64];
        strcpy(display_buf, base_text);
        if (show_dots) {
            for(int i = 0; i < anim_dots; i++) strcat(display_buf, ".");
        }
        
        int text_x = (sw - HAL_Get_Text_Width(display_buf)) / 2;
        int text_y = sh / 2 - 8; 
        HAL_Screen_ShowChineseLine(text_x, text_y, display_buf);
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        anim_dots = 0;
        anim_time = millis();
        is_finished = false;
        
        // 【纯粹的解耦调用】：直接把配置中心的密码塞给底层网络驱动！
        Network_Connect(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());
        
        drawUI((sysConfig.language == 1) ? "启动网络模块" : "INIT NETWORK");
    }

    void onLoop() override {
        if (is_finished) {
            if (millis() - result_show_time > 1500) appManager.popApp();
            return;
        }

        uint32_t now = millis();
        
        // 驱动网络底层状态机干活
        Network_Update(); 
        NetworkState state = Network_GetState();

        if (now - anim_time > 500) {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
            
            if (state == NET_CONNECTING) {
                drawUI((sysConfig.language == 1) ? "连接神经网" : "CONNECTING", true);
            } else if (state == NET_SYNCING_NTP) {
                drawUI((sysConfig.language == 1) ? "校准时间戳" : "SYNCING NTP", true);
            }
        }

        // 根据底层状态决定下一步UI
        if (state == NET_CONNECTED) {
            Network_StartNTP(); 
        } 
        else if (state == NET_SYNC_SUCCESS) {
            drawUI((sysConfig.language == 1) ? "同步已完成!" : "SYNC SUCCESS!");
            HAL_Buzzer_Play_Tone(2000, 80); delay(60); HAL_Buzzer_Play_Tone(2500, 150);
            is_finished = true;
            result_show_time = millis();
        } 
        else if (state == NET_FAIL) {
            drawUI((sysConfig.language == 1) ? "网络连接异常!" : "NETWORK ERROR!");
            HAL_Buzzer_Play_Tone(1000, 300);
            is_finished = true;
            result_show_time = millis();
        }
    }

    void onDestroy() override { Network_Disconnect(); }
    void onKnob(int delta) override {}
    void onKeyShort() override { HAL_Buzzer_Play_Tone(2200, 40); appManager.popApp(); } 
    void onKeyLong() override { appManager.popApp(); }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;