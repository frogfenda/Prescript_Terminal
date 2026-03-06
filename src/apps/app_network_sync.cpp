// 文件：src/apps/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_network.h"

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

        NetworkState state = Network_GetState();
        if (state != NET_CONNECTED && state != NET_SYNC_SUCCESS) {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "错误: 无网络连接!" : "ERR: NO NETWORK!");
            SYS_SOUND_ERROR(); // 【净化】
            is_finished = true;
            result_show_time = millis();
            return;
        }
        
        Network_StartNTP();
        drawUI((appManager.getLanguage() == LANG_ZH) ? "校准时间戳" : "SYNCING NTP", true);
    }

    void onLoop() override {
        if (is_finished) {
            if (millis() - result_show_time > 1500) appManager.popApp();
            return;
        }

        uint32_t now = millis();
        Network_Update(); 
        NetworkState state = Network_GetState();

        if (now - anim_time > 500) {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
            if (state == NET_SYNCING_NTP) {
                drawUI((appManager.getLanguage() == LANG_ZH) ? "校准时间戳" : "SYNCING NTP", true);
            }
        }

        if (state == NET_SYNC_SUCCESS) {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "时间同步完成!" : "TIME SYNCED!");
            // 保留连网成功专属组合音
            HAL_Buzzer_Play_Tone(2000, 80); delay(60); HAL_Buzzer_Play_Tone(2500, 150);
            is_finished = true;
            result_show_time = millis();
        } 
        else if (state == NET_CONNECTED) { 
            drawUI((appManager.getLanguage() == LANG_ZH) ? "服务器无响应!" : "NTP TIMEOUT!");
            SYS_SOUND_ERROR(); // 【净化】
            is_finished = true;
            result_show_time = millis();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override { SYS_SOUND_NAV(); appManager.popApp(); } 
    void onKeyLong() override { appManager.popApp(); }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;