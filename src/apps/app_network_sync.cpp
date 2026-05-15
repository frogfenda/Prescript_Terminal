// 文件：src/apps/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_network.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"

class AppNetworkSync : public AppBase {
private:
    int m_state;
    uint32_t m_timer;
    uint32_t anim_time;
    int anim_dots;

    int last_drawn_state;
    int last_drawn_dots;

    /**
     * 绘制网络同步状态页。
     *
     * 页面由两行居中文本组成：
     * - 第一行是当前动作，例如“连接神经网 / 获取网络时间”；
     * - 第二行是补充说明，例如“请稍候 / NTP 同步中”。
     *
     * 旧版本固定 y=20/40，换成 428×142 后会明显偏上；
     * 当前版本按字体行高计算整块高度，再放到屏幕垂直中心。
     */
    void drawCenteredText(const char *text1, const char *text2, bool show_dots = false) {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        char buf1[64]; strcpy(buf1, text1);
        if (show_dots) {
            for (int i = 0; i < anim_dots; i++) strcat(buf1, ".");
        }

        int body_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        int small_h = HAL_Get_Font_Line_Height(HAL_FONT_SMALL);
        int gap = max(8, sh / 16);
        int block_h = body_h + gap + small_h;
        int y1 = (sh - block_h) / 2;
        int y2 = y1 + body_h + gap;

        int x1 = (sw - HAL_Get_Text_Width(buf1)) / 2;
        int x2 = (sw - HAL_Get_Text_Width_Font(text2, HAL_FONT_SMALL)) / 2;

        HAL_Screen_ShowLine_Font(x1, y1, buf1, HAL_FONT_BODY, TFT_CYAN);
        HAL_Screen_ShowLine_Font(x2, y2, text2, HAL_FONT_SMALL, TFT_DARKGREY);
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        anim_dots = 0; anim_time = millis();
        last_drawn_state = -1; last_drawn_dots = -1;
        m_state = 0;
        
        // 同样是打响指！如果守护神没跑就跑起来，如果在跑了就不会重复触发
        Network_StartSync(); 
    }

    void onLoop() override {
        uint32_t now = millis();
        if (now - anim_time > 500) {
            anim_time = now;
            anim_dots = (anim_dots + 1) % 4;
        }

        NetworkState state = Network_GetState();

        if (state == NET_SYNC_SUCCESS) {
            if (m_state != 2) {
                m_state = 2; m_timer = millis();
                sysAudio.playTone(2000, 80); delay(60); sysAudio.playTone(2500, 150);
            }
            if (millis() - m_timer > 1500) { appManager.popApp(); return; }
        }
        else if (state == NET_CONNECT_FAILED || state == NET_SYNC_FAILED) {
            if (m_state != 3) {
                m_state = 3; m_timer = millis();
                sysAudio.playTone(500, 100);
            }
            if (millis() - m_timer > 2000) { appManager.popApp(); return; }
        }
        else {
            if (state == NET_CONNECTING) m_state = 0;
            else if (state == NET_SYNCING_NTP) m_state = 1;
            else if (state == NET_FETCHING_API) m_state = 4; // 增加 API 拉取状态
        }

        // 【降温核心】：只有状态改变才推给屏幕！
        if (m_state != last_drawn_state || anim_dots != last_drawn_dots) {
            last_drawn_state = m_state;
            last_drawn_dots = anim_dots;

            SystemLang_t lang = appManager.getLanguage();
            if (m_state == 0) drawCenteredText((lang == LANG_ZH) ? "连接神经网" : "CONNECTING", (lang == LANG_ZH) ? "请稍候" : "PLEASE WAIT", true);
            else if (m_state == 1) drawCenteredText((lang == LANG_ZH) ? "获取网络时间" : "SYNCING NTP", (lang == LANG_ZH) ? "NTP 同步中" : "PLEASE WAIT", true);
            else if (m_state == 4) drawCenteredText((lang == LANG_ZH) ? "获取隐秘指令" : "FETCHING API", (lang == LANG_ZH) ? "潜入网络中" : "DOWNLOADING", true);
            else if (m_state == 2) drawCenteredText((lang == LANG_ZH) ? "同步成功!" : "SYNC SUCCESS!", (lang == LANG_ZH) ? "数据已更新" : "DATA UPDATED", false);
            else if (m_state == 3) drawCenteredText((lang == LANG_ZH) ? "同步失败" : "SYNC FAILED", (lang == LANG_ZH) ? "请检查网络" : "CHECK WIFI", false);
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override { SYS_SOUND_NAV(); appManager.popApp(); }
    void onKeyLong() override { appManager.popApp(); }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;