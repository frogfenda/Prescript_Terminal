// 文件：src/app_standby.cpp
#include "app_base.h"
#include "app_manager.h"

class AppStandby : public AppBase {
public:
    void onCreate() override {
        HAL_Screen_Clear();
        HAL_Screen_DrawStandbyImage();
    }
    void onLoop() override {
        // 待机时无动画，不需要做事
    }
    void onDestroy() override {
        // 退出待机时不需要清理特殊内存
    }
    void onKnob(int delta) override {
        // 待机时转旋钮没反应
    }
    void onKeyShort() override {
        HAL_Buzzer_Play_Tone(2000, 40);
        appManager.launchApp(appMainMenu); // 短按进入主菜单！
    }
    void onKeyLong() override {
        // 待机时长按无反应
    }
};

AppStandby instanceStandby;
AppBase* appStandby = &instanceStandby; // 暴露出指针给管家