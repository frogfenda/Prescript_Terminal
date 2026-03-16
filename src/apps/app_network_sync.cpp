// 文件：src/apps/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_network.h"

class AppNetworkSync : public AppBase
{
private:
    int m_state;
    uint32_t m_timer;
    bool m_was_connected;

    uint32_t anim_time;
    int anim_dots;

    // 【降温核心】：记录上一次渲染的状态，用于拦截无效的疯狂重绘
    int last_drawn_state;
    int last_drawn_dots;

    void drawCenteredText(const char *text1, const char *text2, bool show_dots = false)
    {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();
        int sw = HAL_Get_Screen_Width();

        char buf1[64];
        strcpy(buf1, text1);
        if (show_dots)
        {
            for (int i = 0; i < anim_dots; i++)
                strcat(buf1, ".");
        }

        int y1 = 20;
        int y2 = 40;
        int x1 = (sw - HAL_Get_Text_Width(buf1)) / 2;
        int x2 = (sw - HAL_Get_Text_Width(text2)) / 2;

        if (lang == LANG_ZH)
        {
            HAL_Screen_ShowChineseLine(x1, y1, buf1);
            HAL_Screen_ShowChineseLine(x2, y2, text2);
        }
        else
        {
            HAL_Screen_ShowTextLine(x1, y1, buf1);
            HAL_Screen_ShowTextLine(x2, y2, text2);
        }

        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        anim_dots = 0;
        anim_time = millis();
        last_drawn_state = -1; // 初始化为-1，确保第一次必定渲染
        last_drawn_dots = -1;

        NetworkState s = Network_GetState();
        m_was_connected = (s == NET_CONNECTED || s == NET_SYNCING_NTP || s == NET_SYNC_SUCCESS);

        if (m_was_connected)
            Network_ForceNTP();
        else
            Network_Connect();

        m_state = 0;
    }

    void onLoop() override
    {
        uint32_t now = millis();
        if (now - anim_time > 500)
        {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
        }

        NetworkState state = Network_GetState();

        if (state == NET_SYNC_SUCCESS)
        {
            if (m_state != 2)
            {
                m_state = 2;
                m_timer = millis();
                sysAudio.playTone(2000, 80);
                delay(60);
                sysAudio.playTone(2500, 150);
            }
            if (millis() - m_timer > 700)
            {
                if (!m_was_connected)
                    Network_Disconnect();
                appManager.popApp();
                return;
            }
        }
        else if (state == NET_CONNECT_FAILED || state == NET_SYNC_FAILED)
        {
            if (m_state != 3)
            {
                m_state = 3;
                m_timer = millis();
                sysAudio.playTone(500, 100);
            }
            if (millis() - m_timer > 2000)
            {
                if (!m_was_connected)
                    Network_Disconnect();
                appManager.popApp();
                return;
            }
        }
        else
        {
            if (state == NET_CONNECTING)
                m_state = 0;
            else if (state == NET_CONNECTED || state == NET_SYNCING_NTP)
                m_state = 1;
        }

        // 【降温核心】：只有当状态发生改变，或者跳动的点阵发生改变时，才允许推给屏幕！
        // 这一刀直接把屏幕刷新率从 1000 FPS 砍到了 2 FPS，彻底解放 SPI 性能！
        if (m_state != last_drawn_state || anim_dots != last_drawn_dots)
        {
            last_drawn_state = m_state;
            last_drawn_dots = anim_dots;

            SystemLang_t lang = appManager.getLanguage();
            if (m_state == 0)
                drawCenteredText((lang == LANG_ZH) ? "连接神经网" : "CONNECTING", (lang == LANG_ZH) ? "请稍候" : "PLEASE WAIT", true);
            else if (m_state == 1)
                drawCenteredText((lang == LANG_ZH) ? "获取网络时间" : "SYNCING NTP", (lang == LANG_ZH) ? "NTP 同步中" : "PLEASE WAIT", true);
            else if (m_state == 2)
                drawCenteredText((lang == LANG_ZH) ? "同步成功!" : "SYNC SUCCESS!", (lang == LANG_ZH) ? "系统时间已更新" : "TIME UPDATED", false);
            else if (m_state == 3)
                drawCenteredText((lang == LANG_ZH) ? "同步失败" : "SYNC FAILED", (lang == LANG_ZH) ? "请检查网络" : "CHECK WIFI", false);
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override
    {
        SYS_SOUND_NAV();
        if (!m_was_connected && m_state != 2 && m_state != 3)
            Network_Disconnect();
        appManager.popApp();
    }
    void onKeyLong() override
    {
        if (!m_was_connected && m_state != 2 && m_state != 3)
            Network_Disconnect();
        appManager.popApp();
    }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;