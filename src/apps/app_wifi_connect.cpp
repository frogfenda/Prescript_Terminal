// 文件：src/apps/app_wifi_connect.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_network.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"

class AppWifiConnect : public AppBase {
private:
    uint32_t anim_time;
    int anim_dots;
    uint32_t result_show_time;
    bool is_finished;

    void drawUI(const char *base_text, bool show_dots = false) {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        char display_buf[64];
        strcpy(display_buf, base_text);
        if (show_dots) {
            for (int i = 0; i < anim_dots; i++) strcat(display_buf, ".");
        }
        int text_x = (sw - HAL_Get_Text_Width(display_buf)) / 2;
        int text_y = sh / 2 - 8;
        HAL_Screen_ShowChineseLine(text_x, text_y, display_buf);
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        anim_dots = 0; anim_time = millis(); is_finished = false;

        NetworkState state = Network_GetState();
        // 如果守护神已经在跑了，不用重复启动，直接播动画
        if (state == NET_SYNCING_NTP || state == NET_FETCHING_API) {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络已在后台运行" : "WIFI IS RUNNING");
            sysAudio.playTone(1500, 100);
            is_finished = true;
            result_show_time = millis();
            return;
        }

        // 打响指！唤醒 Core 0 网络守护神
        Network_StartSync();
        drawUI((appManager.getLanguage() == LANG_ZH) ? "启动网络模块" : "INIT NETWORK");
    }

    void onLoop() override {
        if (is_finished) {
            if (millis() - result_show_time > 1500) appManager.popApp();
            return;
        }

        uint32_t now = millis();
        if (now - anim_time > 500) {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
        }

        NetworkState state = Network_GetState();

        if (state == NET_CONNECTING) {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "连接神经网" : "CONNECTING", true);
        }
        // 【核心逻辑】：只要度过了 CONNECTING，说明 WiFi 已经连上了！
        // 连网界面的历史使命完成，宣布成功并退出。后台会对时和拉取 API，深藏功与名！
        else if (state == NET_SYNCING_NTP || state == NET_FETCHING_API || state == NET_SYNC_SUCCESS) {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络已接入!" : "WIFI CONNECTED!");
            sysAudio.playTone(2000, 80); delay(60); sysAudio.playTone(2500, 150);
            is_finished = true;
            result_show_time = millis();
        }
        else if (state == NET_CONNECT_FAILED || state == NET_SYNC_FAILED) {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络连接异常!" : "NETWORK ERROR!");
            SYS_SOUND_ERROR();
            is_finished = true;
            result_show_time = millis();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override {
        SYS_SOUND_NAV(); appManager.popApp(); // 即便你强制退出，后台依然在连！
    }
    void onKeyLong() override { appManager.popApp(); }
};

AppWifiConnect instanceWifiConnect;
AppBase *appWifiConnect = &instanceWifiConnect;