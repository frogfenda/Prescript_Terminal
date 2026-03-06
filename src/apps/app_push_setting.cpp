// 文件：src/apps/app_push_setting.cpp
#include "app_menu_base.h" // 【修改】：引入强大的菜单基类！
#include "sys_config.h"
#include "sys_auto_push.h"

class AppPushSetting : public AppMenuBase { // 【修改】：继承 AppMenuBase
private:
    bool is_editing;
    bool t_en;
    int t_min;
    int t_max;

protected:
    int getMenuCount() override { return 4; }

    const char* getTitle() override {
        return (appManager.getLanguage() == LANG_ZH) ? "都市推送设置" : "AUTO PUSH CONFIG";
    }

    const char* getItemText(int index) override {
        static char buf[64];
        
        // 【视觉反馈】：如果正在编辑当前项，在文字末尾加上一个小箭头提示符
        const char* edit_mark = (is_editing && index == current_selection) ? " <" : "";

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) sprintf(buf, "都市意志: %s%s", t_en ? "开启" : "关闭", edit_mark);
            else if (index == 1) sprintf(buf, "最短潜伏: %d 分钟%s", t_min, edit_mark);
            else if (index == 2) sprintf(buf, "最长潜伏: %d 分钟%s", t_max, edit_mark);
            else if (index == 3) strcpy(buf, "保存并接入主系统");
        } else {
            if (index == 0) sprintf(buf, "AUTO PUSH: %s%s", t_en ? "ON" : "OFF", edit_mark);
            else if (index == 1) sprintf(buf, "MIN TIME: %d MIN%s", t_min, edit_mark);
            else if (index == 2) sprintf(buf, "MAX TIME: %d MIN%s", t_max, edit_mark);
            else if (index == 3) strcpy(buf, "SAVE & RETURN");
        }
        return buf;
    }

    void onItemClicked(int index) override {
        if (index == 3) { 
            // 最后一项：保存并退出
            SysAutoPush_UpdateConfig(t_en, t_min, t_max);
            appManager.popApp();
        } else {
            // 其他项：进入/退出 编辑模式
            is_editing = !is_editing;
            drawMenuUI(visual_selection); // 立刻重绘以显示小箭头 <
        }
    }

    void onLongPressed() override { 
        appManager.popApp(); 
    }

public:
    void onCreate() override {
        is_editing = false;
        t_en = sysConfig.auto_push_enable;
        t_min = sysConfig.auto_push_min_min;
        t_max = sysConfig.auto_push_max_min;
        
        // 【关键】：呼叫父类初始化，让菜单引擎开始工作
        AppMenuBase::onCreate();
    }

    // 【核心黑科技：拦截旋钮事件】
    void onKnob(int delta) override {
        if (is_editing) {
            // 如果正在编辑，旋钮用于“改变数值”
            if (current_selection == 0) t_en = !t_en;
            if (current_selection == 1) {
                t_min += delta; 
                if (t_min < 1) t_min = 1;
                if (t_min > t_max) t_max = t_min; // 防呆保护
            }
            if (current_selection == 2) {
                t_max += delta;
                if (t_max < 1) t_max = 1;
                if (t_max < t_min) t_min = t_max; // 防呆保护
            }
            SYS_SOUND_GLITCH();
            drawMenuUI(visual_selection); // 数值改变，手动重绘屏幕
        } else {
            // 如果没在编辑，就把旋钮控制权交还给父类，播放炫酷的 3D 滚动动画！
            AppMenuBase::onKnob(delta);
        }
    }
};

AppPushSetting instancePushSetting;
AppBase* appPushSetting = &instancePushSetting;