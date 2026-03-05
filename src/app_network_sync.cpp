// 文件：src/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"

class AppNetworkSync : public AppBase {
private:
    uint32_t start_time;

    void drawUI() {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        HAL_Screen_ShowChineseLine(40, 84, (UI_Get_Language() == LANG_ZH) ? "网络未配置..." : "Network Offline..."); 
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        drawUI();
        start_time = millis(); 
    }

    void onLoop() override {
        if (millis() - start_time > 1500) {
            appManager.launchApp(appMainMenu);
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override { appManager.launchApp(appMainMenu); }
    void onKeyLong() override { appManager.launchApp(appMainMenu); }
};

AppNetworkSync instanceNetworkSync;
AppBase* appNetworkSync = &instanceNetworkSync;