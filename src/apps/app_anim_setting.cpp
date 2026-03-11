// 文件：src/apps/app_anim_setting.cpp
#include "app_menu_base.h"
#include "sys_config.h" 

class AppAnimSetting : public AppMenuBase { 
protected:
    int getMenuCount() override { return 4; } // 【修改】：选项增加到 4 个

    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "解码动画配置" : "ANIMATION SETUP";
    }

    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            const char* items[] = {"默认矩阵瀑布", "战术游标推进", "逐字乱码扫描", "全局波浪解码"}; // 【新增】：动画四
            return items[index];
        } else {
            const char* items[] = {"MATRIX FALL", "CURSOR TYPEWRITER", "SEQUENTIAL GLITCH", "GLOBAL WAVE"};
            return items[index];
        }
    }

    void onItemClicked(int index) override {
        sysConfig.decode_anim_style = index; 
        sysConfig.save();                    
        appManager.popApp();                 
    }

    void onLongPressed() override { appManager.popApp(); }

public:
    void onCreate() override {
        AppMenuBase::onCreate();
        current_selection = sysConfig.decode_anim_style;
        if (current_selection > 3) current_selection = 0; // 【修改】：边界控制扩展
        
        visual_selection = (float)current_selection;
        drawMenuUI(visual_selection); 
    }
};

AppAnimSetting instanceAnimSetting;
AppBase* appAnimSetting = &instanceAnimSetting;