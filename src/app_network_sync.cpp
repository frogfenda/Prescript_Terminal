// 文件：src/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"

class AppNetworkSync : public AppBase
{
private:
    uint32_t start_time;

    void drawUI()
    {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        HAL_Screen_ShowChineseLine(40, 84, (appManager.getLanguage() == LANG_ZH) ? "网络未配置..." : "Network Offline...");
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        drawUI();
        start_time = millis();
    }

    void onLoop() override
    {
        if (millis() - start_time > 1500)
            appManager.popApp(); // 【修改】：超时原路返回
    }
    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override { appManager.popApp(); } // 【修改】：按键原路返回
    void onKeyLong() override { appManager.popApp(); }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;