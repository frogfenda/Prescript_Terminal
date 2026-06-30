/*
【模块职责】解码动画设置页。用菜单选择 AppPrescript 的四种解码动画模式，并保存到 sysConfig.decode_anim_style。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_anim_setting.cpp
#include "apps/app_menu_base.h"
#include "sys/sys_config.h" 
#include "lang/ui_strings.h"

class AppAnimSetting : public AppMenuBase { 
protected:
    // 【函数说明】返回 4 个解码动画选项，对应 AppPrescript 设置中的四种保留动画。
    int getMenuCount() override { return 4; } // 【修改】：选项增加到 4 个

    const char* getTitle() override {
        return UIStrings::AnimSettingTitle(appManager.getLanguage());
    }

    // 【函数说明】根据 index 返回四种动画模式名称，当前选中的模式会由菜单框高亮。
    const char* getItemText(int index) override {
        return UIStrings::AnimSettingItem(appManager.getLanguage(), index);
    }

    // 【函数说明】点击某一项后把 index 写入 sysConfig.decode_anim_style，保存配置并返回上一页。
    void onItemClicked(int index) override {
        sysConfig.decode_anim_style = index; 
        sysConfig.save();                    
        appManager.popApp();                 
    }

    // 【函数说明】长按退出动画设置页，不改变当前选择。
    void onLongPressed() override { appManager.popApp(); }

public:
    // 【函数说明】进入页面时把菜单光标定位到当前已保存的解码动画模式。
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
