/*
【模块职责】网络同步状态页。启动 Network_StartSync 并以居中文本显示连接、对时、拉取、成功、失败等状态。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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

    // 【函数说明】绘制网络同步状态画面：两行居中文字加可选动态省略号，用于连接/对时/拉取等阶段。
    void drawCenteredText(const char *text1, const char *text2, bool show_dots = false) {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();
        int sw = HAL_Get_Screen_Width();
        char buf1[64]; strcpy(buf1, text1);
        if (show_dots) {
            for (int i = 0; i < anim_dots; i++) strcat(buf1, ".");
        }
        int y1 = 20; int y2 = 40;
        int x1 = (sw - HAL_Get_Text_Width(buf1)) / 2;
        int x2 = (sw - HAL_Get_Text_Width(text2)) / 2;

        if (lang == LANG_ZH) {
            HAL_Screen_ShowChineseLine(x1, y1, buf1);
            HAL_Screen_ShowChineseLine(x2, y2, text2);
        } else {
            HAL_Screen_ShowTextLine(x1, y1, buf1);
            HAL_Screen_ShowTextLine(x2, y2, text2);
        }
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入网络同步页后立即调用 Network_StartSync，并显示初始连接状态。
    void onCreate() override {
        anim_dots = 0; anim_time = millis();
        last_drawn_state = -1; last_drawn_dots = -1;
        m_state = 0;
        
        // 同样是打响指！如果守护神没跑就跑起来，如果在跑了就不会重复触发
        Network_StartSync(); 
    }

    // 【函数说明】轮询 Network_GetState，状态变化或省略号动画变化时重绘，并在成功/失败后给出反馈。
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
                Feedback_PlayNetworkOk();
            }
            if (millis() - m_timer > 1500) { appManager.popApp(); return; }
        }
        else if (state == NET_CONNECT_FAILED || state == NET_SYNC_FAILED) {
            if (m_state != 3) {
                m_state = 3; m_timer = millis();
                Feedback_PlayNetworkError();
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
    // 【函数说明】短按从网络同步页面返回上一页。
    void onKeyShort() override { SYS_SOUND_NAV(); appManager.popApp(); }
    void onKeyLong() override { appManager.popApp(); }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;
