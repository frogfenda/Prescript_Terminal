/*
【模块职责】本地使用者应用。

本 App 只负责设备本地菜单交互：
- 短按 ID：设为当前使用者；
- 长按 ID：进入删除确认；
- 确认态短按：删除；
- 确认态长按：取消。

ID 的保存、清洗、蓝牙同步和“致...”文本替换都在 sys_prescript_target 服务中完成。
*/
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "sys/sys_config.h"
#include "sys/sys_prescript_target.h"
#include "ui/ui_frame.h"
#include "lang/ui_strings.h"

class AppPrescriptTarget : public AppMenuBase
{
private:
    bool deleting = false;
    bool waiting_delete_key_release = false;
    uint32_t delete_redraw_tick = 0;

    bool isClearIndex(int index) const
    {
        return index == sysConfig.prescript_target_count;
    }

    bool isBackIndex(int index) const
    {
        return index == sysConfig.prescript_target_count + 1;
    }

    void drawDeleteConfirm()
    {
        HAL_Sprite_Clear();

        SystemLang_t lang = appManager.getLanguage();
        String id = (current_selection >= 0 && current_selection < sysConfig.prescript_target_count)
                        ? sysConfig.prescript_targets[current_selection]
                        : "";
        String message = UIStrings::IsZh(lang) ? ("删除 [" + id + "] ?") : ("DEL [" + id + "] ?");

        UIFrame::DrawDangerConfirm(
            UIStrings::DangerTitle(lang),
            message.c_str(),
            UIStrings::DeleteUserHint(lang));

        HAL_Screen_Update();
        delete_redraw_tick = millis();
    }

protected:
    int getMenuCount() override
    {
        // 所有 ID + 清空当前使用者 + 返回。
        return sysConfig.prescript_target_count + 2;
    }

    const char *getTitle() override
    {
        return UIStrings::UserTitle(appManager.getLanguage());
    }

    const char *getItemText(int index) override
    {
        SystemLang_t lang = appManager.getLanguage();
        static char buf[96];

        if (isClearIndex(index))
            return UIStrings::ClearCurrentUserItem(lang);
        if (isBackIndex(index))
            return UIStrings::BackItem(lang);

        String id = sysConfig.prescript_targets[index];
        bool selected = (id == sysConfig.current_prescript_target);
        snprintf(buf, sizeof(buf), "%s%s", id.c_str(), selected ? UIStrings::CurrentUserSuffix(lang) : "");
        return buf;
    }

    void onItemClicked(int index) override
    {
        if (deleting)
        {
            if (waiting_delete_key_release)
                return;

            if (current_selection >= 0 && current_selection < sysConfig.prescript_target_count)
            {
                String id = sysConfig.prescript_targets[current_selection];
                SysPrescriptTarget_Delete(id);
                if (current_selection >= getMenuCount())
                    current_selection = getMenuCount() - 1;
                if (current_selection < 0)
                    current_selection = 0;
            }
            deleting = false;
            drawMenuUI(visual_selection);
            return;
        }

        if (isBackIndex(index))
        {
            appManager.popApp();
            return;
        }

        if (isClearIndex(index))
        {
            SysPrescriptTarget_SetCurrent("");
            drawMenuUI(visual_selection);
            return;
        }

        SysPrescriptTarget_SetCurrent(sysConfig.prescript_targets[index]);
        drawMenuUI(visual_selection);
    }

    void onLongPressed() override
    {
        if (deleting)
        {
            if (waiting_delete_key_release)
                return;

            deleting = false;
            SYS_SOUND_NAV();
            drawMenuUI(visual_selection);
            return;
        }

        if (current_selection >= 0 && current_selection < sysConfig.prescript_target_count)
        {
            deleting = true;
            waiting_delete_key_release = true;
            SYS_SOUND_GLITCH();
            drawDeleteConfirm();
            return;
        }

        appManager.popApp();
    }

public:
    void onCreate() override
    {
        deleting = false;
        waiting_delete_key_release = false;
        AppMenuBase::onCreate();
    }

    void onResume() override
    {
        deleting = false;
        waiting_delete_key_release = false;
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }

    void onKnob(int delta) override
    {
        if (deleting)
            return;
        AppMenuBase::onKnob(delta);
    }

    void onLoop() override
    {
        if (!deleting)
        {
            AppMenuBase::onLoop();
            return;
        }

        if (waiting_delete_key_release && !HAL_Is_Key_Pressed())
            waiting_delete_key_release = false;

        // 确认态不走菜单基类重绘，避免 HUD 或菜单动画把确认弹窗盖掉。
        if (millis() - delete_redraw_tick > 500)
            drawDeleteConfirm();
    }
};

AppPrescriptTarget instancePrescriptTarget;
AppBase *appPrescriptTarget = &instancePrescriptTarget;
