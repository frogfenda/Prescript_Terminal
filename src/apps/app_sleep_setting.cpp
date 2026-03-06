// 文件：src/apps/app_sleep_setting.cpp
#include "app_menu_base.h" // 【核心升级】：直接接入 3D 菜单引擎！
#include "sys_config.h" 

class AppSleepSetting : public AppMenuBase { // 继承自菜单基类
protected:
    int getMenuCount() override { return 4; }

    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "休眠时间设定" : "SLEEP SETTINGS";
    }

    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            const char* items[] = {"30 秒 (推荐)", "60 秒", "5 分钟", "永不休眠"};
            return items[index];
        } else {
            const char* items[] = {"30 SECONDS", "60 SECONDS", "5 MINUTES", "NEVER SLEEP"};
            return items[index];
        }
    }

    void onItemClicked(int index) override {
        switch (index) {
            case 0: appManager.config_sleep_time_ms = 30000; break;
            case 1: appManager.config_sleep_time_ms = 60000; break;
            case 2: appManager.config_sleep_time_ms = 300000; break;
            case 3: appManager.config_sleep_time_ms = 0xFFFFFFFF; break;
        }
        
        sysConfig.sleep_time_ms = appManager.config_sleep_time_ms;
        sysConfig.save();
        
        appManager.popApp(); 
    }

    void onLongPressed() override { appManager.popApp(); }

public:
    void onCreate() override {
        // 先让父类初始化
        AppMenuBase::onCreate();
        
        // 【细节优化】：每次点进菜单，自动把焦点对准当前保存的配置！
        uint32_t current_sleep = sysConfig.sleep_time_ms;
        if (current_sleep == 60000) current_selection = 1;
        else if (current_sleep == 300000) current_selection = 2;
        else if (current_sleep == 0xFFFFFFFF) current_selection = 3;
        else current_selection = 0; // 默认 30 秒
        
        visual_selection = (float)current_selection;
        
        // 强制重绘一帧，让焦点直接落位
        drawMenuUI(visual_selection); 
    }
    
    // 因为继承了 AppMenuBase，onKnob、onLoop、动画和防频闪逻辑全部由父类接管！
    // 这个文件再也不需要写任何画图和动画的代码了！
};

AppSleepSetting instanceSleepSetting;
AppBase* appSleepSetting = &instanceSleepSetting;