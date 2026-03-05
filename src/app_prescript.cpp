// 文件：src/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "prescript_logic.h"

class AppPrescript : public AppBase {
public:
    void onCreate() override {
        // 进入应用时，直接触发第一次指令抽取
        Prescript_Action();
    }

    void onLoop() override {
        // 静态显示文本，不需要持续刷动画
    }

    void onDestroy() override {}

    void onKnob(int delta) override {
        // 浏览指令时，转动旋钮暂无动作（未来如果指令太长，可以在这里加翻页逻辑）
    }

    void onKeyShort() override {
        // 短按：重新抽取指令
        HAL_Buzzer_Play_Tone(2200, 40);
        Prescript_Action();
    }

    void onKeyLong() override {
        // 长按：退回主菜单
        appManager.launchApp(appMainMenu);
    }
};

AppPrescript instancePrescript;
AppBase* appPrescript = &instancePrescript;