/*
【模块职责】自动推送设置页。配置是否启用、最小间隔和最大间隔，并调用 SysAutoPush_UpdateConfig 重新安排下一次推送。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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
    // 【函数说明】返回自动推送设置的四项：开关、最小间隔、最大间隔、返回。
    int getMenuCount() override { return 4; }

    const char* getTitle() override {
        // 【改名】：由都市推送改为指令推送
        return (appManager.getLanguage() == LANG_ZH) ? "指令推送配置" : "PUSH CONFIG";
    }

    // 【函数说明】返回每个设置项的完整显示文本。
    const char* getItemText(int index) override {
        static char buf[64];
        const char* edit_mark = (is_editing && index == current_selection) ? " <" : "";

        if (appManager.getLanguage() == LANG_ZH) {
            // 【改名】
            if (index == 0) sprintf(buf, "指令推送: %s%s", t_en ? "开启" : "关闭", edit_mark);
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

    // 【函数说明】把可编辑条目拆成前缀、动态值、后缀，让 AppMenuBase 只对数字部分做跳动动画。
    bool getItemEditParts(int index, const char** prefix, const char** anim_val, const char** suffix) override {
        if (!is_editing || index != current_selection) return false;
        
        static char buf_val[16];
        static char buf_pref[32];
        static char buf_suff[32];

        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) {
                strcpy(buf_pref, "指令推送: "); // 【改名】
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
        return true; 
    }

    // 【函数说明】短按切换编辑状态或保存并返回；开关项直接翻转启用状态。
    void onItemClicked(int index) override {
        if (index == 3) { 
            SysAutoPush_UpdateConfig(t_en, t_min, t_max);
            appManager.popApp();
        } else {
            is_editing = !is_editing;
            drawMenuUI(visual_selection); 
        }
    }

    // 【函数说明】长按保存自动推送配置，调用 SysAutoPush_UpdateConfig 重置下一次推送。
    void onLongPressed() override { 
    // 【修复防呆】：不管你是怎么退出的，哪怕是长按强行返回，也强制写入硬盘！
    SysAutoPush_UpdateConfig(t_en, t_min, t_max);
    appManager.popApp(); 
}

public:
    // 【函数说明】进入设置页时从 sysConfig 读取当前自动推送开关和间隔范围。
    void onCreate() override {
        is_editing = false;
        t_en = sysConfig.auto_push_enable;
        t_min = sysConfig.auto_push_min_min;
        t_max = sysConfig.auto_push_max_min;
        AppMenuBase::onCreate();
    }

    // 【函数说明】编辑状态下旋钮调整最小/最大推送间隔，并保证最大值不小于最小值。
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
            triggerEditAnimation(delta); 
        } else {
            AppMenuBase::onKnob(delta);
        }
    }
};

AppPushSetting instancePushSetting;
AppBase* appPushSetting = &instancePushSetting;
