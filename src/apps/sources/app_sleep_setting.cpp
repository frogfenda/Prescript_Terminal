/*
【模块职责】休眠设置页。配置空闲进入待机的时间和待机后真正 Light Sleep 的时间。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_sleep_setting.cpp
#include "apps/app_menu_base.h"
#include "sys/sys_constants.h"
#include "sys/sys_config.h" 
#include "lang/ui_strings.h"

class AppSleepSetting : public AppMenuBase {
private:
    int menu_level = 0; // 0:主级菜单, 1:待机时间选项, 2:深睡时间选项

protected:
    // 【函数说明】根据 menu_level 返回当前层级条目数：主层级、待机时间候选、真休眠时间候选。
    int getMenuCount() override {
        if (menu_level == 0) return 2; // 主菜单只有 2 项
        return 4;                      // 子菜单有 4 个选项
    }

    // 【函数说明】按层级返回休眠设置标题，让用户知道当前正在配置待机还是深睡。
    const char* getTitle() override {
        return UIStrings::SleepSettingTitle(appManager.getLanguage(), menu_level);
    }

    // 【函数说明】返回当前层级的候选文本，例如 30 秒、1 分钟、永不休眠。
    const char* getItemText(int index) override {
        return UIStrings::SleepSettingItem(appManager.getLanguage(), menu_level, index);
    }

    // 【函数说明】主层级进入具体时间选择；时间层级写入 sysConfig.sleep_time_ms/true_sleep_time_ms 并保存。
    void onItemClicked(int index) override {
        if (menu_level == 0) {
            // 【进入二级菜单】
            menu_level = index + 1; // 选第一项进入 level 1，选第二项进入 level 2
            
            // 自动对焦到当前硬盘里保存的数值
            if (menu_level == 1) {
                uint32_t t = sysConfig.sleep_time_ms;
                if (t == 60000) current_selection = 1;
                else if (t == 300000) current_selection = 2;
                else if (t == PrescriptConst::NEVER_SLEEP_MS) current_selection = 3;
                else current_selection = 0;
            } else {
                uint32_t t = sysConfig.true_sleep_time_ms;
                if (t == 30000) current_selection = 1;
                else if (t == 60000) current_selection = 2;
                else if (t == PrescriptConst::NEVER_SLEEP_MS) current_selection = 3;
                else current_selection = 0;
            }
            visual_selection = (float)current_selection;

        } else if (menu_level == 1) {
            // 【保存待机时间并返回】
            switch (index) {
                case 0: appManager.config_sleep_time_ms = 30000; break;
                case 1: appManager.config_sleep_time_ms = 60000; break;
                case 2: appManager.config_sleep_time_ms = 300000; break;
                case 3: appManager.config_sleep_time_ms = PrescriptConst::NEVER_SLEEP_MS; break;
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
                case 3: sysConfig.true_sleep_time_ms = PrescriptConst::NEVER_SLEEP_MS; break;
            }
            sysConfig.save();
            
            menu_level = 0; current_selection = 1; visual_selection = 1; // 退回主菜单并定焦
        }
        
        drawMenuUI(visual_selection); // 强制重绘刷新
    }

    // 【函数说明】长按在子层级返回主层级，在主层级退出设置页。
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
    // 【函数说明】进入休眠设置页时重置到主层级并绘制菜单。
    void onCreate() override {
        menu_level = 0;
        current_selection = 0;
        visual_selection = 0;
        AppMenuBase::onCreate();
    }
};

AppSleepSetting instanceSleepSetting;
AppBase* appSleepSetting = &instanceSleepSetting;
