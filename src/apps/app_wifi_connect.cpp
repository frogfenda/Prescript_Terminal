// 文件：src/apps/app_wifi_connect.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_network.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"
#include <WiFi.h> // 【关键修复】：必须包含WiFi库，否则无法执行断网指令

class AppWifiConnect : public AppBase
{
private:
    uint32_t anim_time;
    int anim_dots;
    uint32_t result_show_time;
    bool is_finished;

    void drawUI(const char *base_text, bool show_dots = false)
    {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        char display_buf[64];
        strcpy(display_buf, base_text);
        if (show_dots)
        {
            for (int i = 0; i < anim_dots; i++)
                strcat(display_buf, ".");
        }
        int text_x = (sw - HAL_Get_Text_Width(display_buf)) / 2;
        int text_y = sh / 2 - 8;
        HAL_Screen_ShowChineseLine(text_x, text_y, display_buf);
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        anim_dots = 0;
        anim_time = millis();
        is_finished = false;
        NetworkState state = Network_GetState();

        // 1. 如果已经连上 -> 强行断网
        if (state == NET_SYNC_SUCCESS)
        {
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);
            extern volatile NetworkState g_state;
            g_state = NET_DISCONNECTED; // 强制更新底层状态

            drawUI((appManager.getLanguage() == LANG_ZH) ? "已切断神经网" : "WIFI DISCONNECTED");
            sysAudio.playTone(800, 100);
            is_finished = true;
            result_show_time = millis();
            return;
        }
        // 2. 如果守护神正在干活 -> 提示运行中
        else if (state == NET_CONNECTING || state == NET_SYNCING_NTP || state == NET_FETCHING_API)
        {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络已在后台运行" : "WIFI IS RUNNING");
            sysAudio.playTone(1500, 100);
            is_finished = true;
            result_show_time = millis();
            return;
        }

        // 3. 只有断网或失败时 -> 发起手动常驻连接 (传入 true)
        Network_StartSync(true);
        drawUI((appManager.getLanguage() == LANG_ZH) ? "启动网络模块" : "INIT NETWORK");
    }

    void onLoop() override
    {
        if (is_finished)
        {
            if (millis() - result_show_time > 1500)
                appManager.popApp();
            return;
        }

        uint32_t now = millis();
        bool dots_changed = false;
        if (now - anim_time > 500)
        {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
            dots_changed = true; // 脏矩形标记
        }

        NetworkState state = Network_GetState();

        // 【核心修复】：只有点数变化时才允许重绘，彻底告别频闪！
        if (dots_changed && state == NET_CONNECTING)
        {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "连接神经网" : "CONNECTING", true);
        }
        else if (state == NET_SYNCING_NTP || state == NET_FETCHING_API || state == NET_SYNC_SUCCESS)
        {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络已接入!" : "WIFI CONNECTED!");
            sysAudio.playTone(2000, 80);
            delay(60);
            sysAudio.playTone(2500, 150);
            is_finished = true;
            result_show_time = millis();
        }
        else if (state == NET_CONNECT_FAILED || state == NET_SYNC_FAILED)
        {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络连接异常!" : "NETWORK ERROR!");
            SYS_SOUND_ERROR();
            is_finished = true;
            result_show_time = millis();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override
    {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
    void onKeyLong() override { appManager.popApp(); }
};

AppWifiConnect instanceWifiConnect;
AppBase *appWifiConnect = &instanceWifiConnect;