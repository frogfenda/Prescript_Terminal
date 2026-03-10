// 文件：src/apps/app_anim_setting.cpp
#include "app_menu_base.h"
#include "sys_config.h" 

class AppAnimSetting : public AppMenuBase { 
protected:
    int getMenuCount() override { return 2; }

    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "解码动画配置" : "ANIMATION SETUP";
    }

    const char* getItemText(int index) override {
        if (appManager.getLanguage() == LANG_ZH) {
            const char* items[] = {"默认矩阵瀑布", "二号战术动画"};
            return items[index];
        } else {
            const char* items[] = {"MATRIX FALL", "TACTICAL TYPE II"};
            return items[index];
        }
    }

    void onItemClicked(int index) override {
        sysConfig.decode_anim_style = index; // 将选择写入配置
        sysConfig.save();                    // 保存到物理存储
        appManager.popApp();                 // 退出当前页面
    }

    void onLongPressed() override { appManager.popApp(); }

public:
    void onCreate() override {
        AppMenuBase::onCreate();
        
        // 进入页面时，默认指向当前配置的动画
        current_selection = sysConfig.decode_anim_style;
        if (current_selection > 1) current_selection = 0; 
        
        visual_selection = (float)current_selection;
        drawMenuUI(visual_selection); 
    }
};

AppAnimSetting instanceAnimSetting;
AppBase* appAnimSetting = &instanceAnimSetting;