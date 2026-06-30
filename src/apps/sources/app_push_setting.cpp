/*
【模块职责】自动推送设置页。配置是否启用、最小间隔和最大间隔，并调用 SysAutoPush_UpdateConfig 重新安排下一次推送。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_push_setting.cpp
#include "apps/app_menu_base.h" 
#include "sys/sys_config.h"
#include "sys/sys_auto_push.h"
#include "lang/ui_strings.h"

class AppPushSetting : public AppMenuBase { 
private:
    bool is_editing;
    bool t_en;
    int t_min;
    int t_max;

protected:
    // 【函数说明】返回自动推送设置的五项：开关、最小间隔、最大间隔、使用者、保存返回。
    int getMenuCount() override { return 5; }

    const char* getTitle() override {
        return UIStrings::PushSettingTitle(appManager.getLanguage());
    }

    // 【函数说明】返回每个设置项的完整显示文本。
    const char* getItemText(int index) override {
        static char buf[64];
        const char* edit_mark = (is_editing && index == current_selection) ? " <" : "";
        SystemLang_t lang = appManager.getLanguage();

        if (index == 0) snprintf(buf, sizeof(buf), "%s%s%s", UIStrings::PushEnableLabel(lang), UIStrings::OnOff(lang, t_en), edit_mark);
        else if (index == 1) snprintf(buf, sizeof(buf), "%s%d%s%s", UIStrings::PushMinLabel(lang), t_min, UIStrings::PushMinuteSuffix(lang), edit_mark);
        else if (index == 2) snprintf(buf, sizeof(buf), "%s%d%s%s", UIStrings::PushMaxLabel(lang), t_max, UIStrings::PushMinuteSuffix(lang), edit_mark);
        else if (index == 3) strncpy(buf, UIStrings::PushUserItem(lang), sizeof(buf));
        else if (index == 4) strncpy(buf, UIStrings::SaveAndReturnItem(lang), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }

    // 【函数说明】把可编辑条目拆成前缀、动态值、后缀，让 AppMenuBase 只对数字部分做跳动动画。
    bool getItemEditParts(int index, const char** prefix, const char** anim_val, const char** suffix) override {
        if (!is_editing || index != current_selection) return false;
        
        static char buf_val[16];
        static char buf_pref[32];
        static char buf_suff[32];
        SystemLang_t lang = appManager.getLanguage();

        if (index == 0) {
            strcpy(buf_pref, UIStrings::PushEnableLabel(lang));
            strcpy(buf_val, UIStrings::OnOff(lang, t_en));
            strcpy(buf_suff, " <");
        } else if (index == 1) {
            strcpy(buf_pref, UIStrings::PushMinLabel(lang));
            snprintf(buf_val, sizeof(buf_val), "%d", t_min);
            snprintf(buf_suff, sizeof(buf_suff), "%s <", UIStrings::PushMinuteSuffix(lang));
        } else if (index == 2) {
            strcpy(buf_pref, UIStrings::PushMaxLabel(lang));
            snprintf(buf_val, sizeof(buf_val), "%d", t_max);
            snprintf(buf_suff, sizeof(buf_suff), "%s <", UIStrings::PushMinuteSuffix(lang));
        } else return false;
        
        *prefix = buf_pref;
        *anim_val = buf_val;
        *suffix = buf_suff;
        return true; 
    }

    // 【函数说明】短按切换编辑状态或保存并返回；开关项直接翻转启用状态。
    void onItemClicked(int index) override {
        if (index == 4) { 
            SysAutoPush_UpdateConfig(t_en, t_min, t_max);
            appManager.popApp();
        } else if (index == 3) {
            SysAutoPush_UpdateConfig(t_en, t_min, t_max);
            appManager.push(AppId::PrescriptTarget);
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
