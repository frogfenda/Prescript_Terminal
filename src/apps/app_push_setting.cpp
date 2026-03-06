// 文件：src/apps/app_push_setting.cpp
#include "app_menu_base.h" 
#include "sys_config.h"
#include "sys_auto_push.h"

class AppPushSetting : public AppMenuBase { 
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

    // 依然保留完整的文字，用于菜单引擎计算总居中宽度
    const char* getItemText(int index) override {
        static char buf[64];
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

    // 【新增核心】：精准剥离需要跳动的核心变量！
    bool getItemEditParts(int index, const char** prefix, const char** anim_val, const char** suffix) override {
        if (!is_editing || index != current_selection) return false;
        
        static char buf_val[16];
        static char buf_pref[32];
        static char buf_suff[32];

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) {
                strcpy(buf_pref, "都市意志: ");
                strcpy(buf_val, t_en ? "开启" : "关闭");
                strcpy(buf_suff, " <");
            } else if (index == 1) {
                strcpy(buf_pref, "最短潜伏: ");
                sprintf(buf_val, "%d", t_min);
                strcpy(buf_suff, " 分钟 <");
            } else if (index == 2) {
                strcpy(buf_pref, "最长潜伏: ");
                sprintf(buf_val, "%d", t_max);
                strcpy(buf_suff, " 分钟 <");
            } else return false;
        } else {
            if (index == 0) {
                strcpy(buf_pref, "AUTO PUSH: ");
                strcpy(buf_val, t_en ? "ON" : "OFF");
                strcpy(buf_suff, " <");
            } else if (index == 1) {
                strcpy(buf_pref, "MIN TIME: ");
                sprintf(buf_val, "%d", t_min);
                strcpy(buf_suff, " MIN <");
            } else if (index == 2) {
                strcpy(buf_pref, "MAX TIME: ");
                sprintf(buf_val, "%d", t_max);
                strcpy(buf_suff, " MIN <");
            } else return false;
        }
        
        *prefix = buf_pref;
        *anim_val = buf_val;
        *suffix = buf_suff;
        return true; // 告诉父类：请帮我分段跳动！
    }

    void onItemClicked(int index) override {
        if (index == 3) { 
            SysAutoPush_UpdateConfig(t_en, t_min, t_max);
            appManager.popApp();
        } else {
            is_editing = !is_editing;
            drawMenuUI(visual_selection); 
        }
    }

    void onLongPressed() override { appManager.popApp(); }

public:
    void onCreate() override {
        is_editing = false;
        t_en = sysConfig.auto_push_enable;
        t_min = sysConfig.auto_push_min_min;
        t_max = sysConfig.auto_push_max_min;
        AppMenuBase::onCreate();
    }

    void onKnob(int delta) override {
        if (is_editing) {
            if (current_selection == 0) t_en = !t_en;
            if (current_selection == 1) {
                t_min += delta; 
                if (t_min < 1) t_min = 1;
                if (t_min > t_max) t_max = t_min; 
            }
            if (current_selection == 2) {
                t_max += delta;
                if (t_max < 1) t_max = 1;
                if (t_max < t_min) t_min = t_max; 
            }
            SYS_SOUND_GLITCH();
            triggerEditAnimation(delta); // 呼叫跳动动画
        } else {
            AppMenuBase::onKnob(delta);
        }
    }
};

AppPushSetting instancePushSetting;
AppBase* appPushSetting = &instancePushSetting;