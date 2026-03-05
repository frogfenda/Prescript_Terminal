// 文件：src/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"

class AppNetworkSync : public AppBase {
private:
    uint32_t start_time;

public:
    void onCreate() override {
        UI_DrawNetworkSyncScreen();
        start_time = millis(); // 记录进入应用的时间
    }

    void onLoop() override {
        // 非阻塞判断：如果时间超过 1.5 秒，自动退出
        if (millis() - start_time > 1500) {
            appManager.launchApp(appMainMenu);
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override {}

    void onKeyShort() override {
        // 还没到 1.5 秒就不想等了，直接短按退出
        appManager.launchApp(appMainMenu);
    }

    void onKeyLong() override {
        appManager.launchApp(appMainMenu);
    }
};

AppNetworkSync instanceNetworkSync;
AppBase* appNetworkSync = &instanceNetworkSync;