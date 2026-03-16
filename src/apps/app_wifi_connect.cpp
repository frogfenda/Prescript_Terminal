// 文件：src/apps/app_wifi_connect.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_config.h"
#include "sys_network.h"

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
        if (state == NET_CONNECTED || state == NET_SYNC_SUCCESS || state == NET_SYNCING_NTP)
        {
            Network_Disconnect();
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络已切断" : "WIFI DISCONNECTED");
            sysAudio.playTone(1500, 100); // 专属断开提示音保留
            is_finished = true;
            result_show_time = millis();
            return;
        }

        // 【修复 1】：去掉多余的参数，底层会自动读取配置
        Network_Connect();
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
        Network_Update();
        NetworkState state = Network_GetState();

        if (now - anim_time > 500)
        {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
            if (state == NET_CONNECTING)
            {
                drawUI((appManager.getLanguage() == LANG_ZH) ? "连接神经网" : "CONNECTING", true);
            }
        }

        if (state == NET_CONNECTED || state == NET_SYNCING_NTP || state == NET_SYNC_SUCCESS)
        {
            drawUI((appManager.getLanguage() == LANG_ZH) ? "网络已接入!" : "WIFI CONNECTED!");
            // 专属开机组合音乐保留
            sysAudio.playTone(2000, 80);
            delay(60);
            sysAudio.playTone(2500, 150);
            is_finished = true;
            result_show_time = millis();
        }
        // 【修复 2】：适配最新的错误枚举状态
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