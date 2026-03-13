// 文件：src/apps/app_sleep_setting.cpp
#include "app_menu_base.h"
#include "sys_config.h" 

class AppSleepSetting : public AppMenuBase {
private:
    int menu_level = 0; // 0:主级菜单, 1:待机时间选项, 2:深睡时间选项

protected:
    int getMenuCount() override {
        if (menu_level == 0) return 2; // 主菜单只有 2 项
        return 4;                      // 子菜单有 4 个选项
    }

    const char* getTitle() override {
        if (appManager.getLanguage() == LANG_ZH) {
            if (menu_level == 0) return "电源与休眠策略";
            if (menu_level == 1) return "待机触发 (亮屏展示)";
            if (menu_level == 2) return "深度休眠 (物理黑屏)";
        } else {
            if (menu_level == 0) return "POWER POLICY";
            if (menu_level == 1) return "STANDBY (SCREEN ON)";
            if (menu_level == 2) return "DEEP SLEEP (SCREEN OFF)";
        }
        return "";
    }

    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            if (menu_level == 0) {
                const char* items[] = {"设定待机时间", "设定深度休眠"};
                return items[index];
            } else if (menu_level == 1) { // 待机时间选项
                const char* items[] = {"30 秒", "60 秒", "5 分钟", "永不待机"};
                return items[index];
            } else { // 深度休眠选项
                const char* items[] = {"3 秒 (立刻)", "30 秒", "60 秒", "永不休眠"};
                return items[index];
            }
        } else {
            if (menu_level == 0) {
                const char* items[] = {"STANDBY TIMEOUT", "SLEEP TIMEOUT"};
                return items[index];
            } else if (menu_level == 1) {
                const char* items[] = {"30 SEC", "60 SEC", "5 MIN", "NEVER"};
                return items[index];
            } else {
                const char* items[] = {"3 SEC (INSTANT)", "30 SEC", "60 SEC", "NEVER"};
                return items[index];
            }
        }
    }

    void onItemClicked(int index) override {
        if (menu_level == 0) {
            // 【进入二级菜单】
            menu_level = index + 1; // 选第一项进入 level 1，选第二项进入 level 2
            
            // 自动对焦到当前硬盘里保存的数值
            if (menu_level == 1) {
                uint32_t t = sysConfig.sleep_time_ms;
                if (t == 60000) current_selection = 1;
                else if (t == 300000) current_selection = 2;
                else if (t == 0xFFFFFFFF) current_selection = 3;
                else current_selection = 0;
            } else {
                uint32_t t = sysConfig.true_sleep_time_ms;
                if (t == 30000) current_selection = 1;
                else if (t == 60000) current_selection = 2;
                else if (t == 0xFFFFFFFF) current_selection = 3;
                else current_selection = 0;
            }
            visual_selection = (float)current_selection;

        } else if (menu_level == 1) {
            // 【保存待机时间并返回】
            switch (index) {
                case 0: appManager.config_sleep_time_ms = 30000; break;
                case 1: appManager.config_sleep_time_ms = 60000; break;
                case 2: appManager.config_sleep_time_ms = 300000; break;
                case 3: appManager.config_sleep_time_ms = 0xFFFFFFFF; break;
            }
            sysConfig.sleep_time_ms = appManager.config_sleep_time_ms;
            sysConfig.save();
            
            menu_level = 0; current_selection = 0; visual_selection = 0; // 退回主菜单
        } else if (menu_level == 2) {
            // 【保存深度休眠时间并返回】
            switch (index) {
                case 0: sysConfig.true_sleep_time_ms = 3000; break;
                case 1: sysConfig.true_sleep_time_ms = 30000; break;
                case 2: sysConfig.true_sleep_time_ms = 60000; break;
                case 3: sysConfig.true_sleep_time_ms = 0xFFFFFFFF; break;
            }
            sysConfig.save();
            
            menu_level = 0; current_selection = 1; visual_selection = 1; // 退回主菜单并定焦
        }
        
        drawMenuUI(visual_selection); // 强制重绘刷新
    }

    void onLongPressed() override {
        if (menu_level == 0) {
            appManager.popApp(); // 在主级菜单长按：退出应用
        } else {
            // 在二级菜单长按：退回一级菜单
            menu_level = 0;
            current_selection = 0;
            visual_selection = 0;
            drawMenuUI(visual_selection);
        }
    }

public:
    void onCreate() override {
        menu_level = 0;
        current_selection = 0;
        visual_selection = 0;
        AppMenuBase::onCreate();
    }
};

AppSleepSetting instanceSleepSetting;
AppBase* appSleepSetting = &instanceSleepSetting;