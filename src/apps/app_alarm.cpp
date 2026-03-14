// 文件：src/apps/app_alarm.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"

int g_alarm_edit_idx = -1;
extern AppBase *appAlarmEdit;

void Alarm_AddPresetMobile(const char *name, int hour, int min, const char *text)
{
    if (sysConfig.alarm_count >= 10)
        return;
    int idx = sysConfig.alarm_count;
    sysConfig.alarms[idx].hour = hour;
    sysConfig.alarms[idx].min = min;
    sysConfig.alarms[idx].is_active = true;
    sysConfig.alarms[idx].name = name;
    sysConfig.alarms[idx].prescript = text;
    sysConfig.alarm_count++;
    sysConfig.save();
}

void Alarm_DeleteMobile(const char *name)
{
    bool deleted = false;
    for (int i = 0; i < sysConfig.alarm_count; i++)
    {
        if (sysConfig.alarms[i].name == name)
        {
            for (int j = i; j < sysConfig.alarm_count - 1; j++)
            {
                sysConfig.alarms[j] = sysConfig.alarms[j + 1];
            }
            sysConfig.alarm_count--;
            deleted = true;
            i--;
        }
    }
    if (deleted)
        sysConfig.save();
}

void Alarm_UpdateBackground()
{
    static uint32_t last_check = 0;
    if (millis() - last_check < 1000)
        return;
    last_check = millis();

    time_t now;
    time(&now);
    if (now < 1000000000)
        return;

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    static int last_trigger_min = -1;
    static bool missed_alarm_pending = false;
    static int pending_alarm_idx = -1;

    if (timeinfo.tm_min != last_trigger_min)
    {
        for (int i = 0; i < sysConfig.alarm_count; i++)
        {
            if (sysConfig.alarms[i].is_active && sysConfig.alarms[i].hour == timeinfo.tm_hour && sysConfig.alarms[i].min == timeinfo.tm_min)
            {
                missed_alarm_pending = true;
                pending_alarm_idx = i;
                last_trigger_min = timeinfo.tm_min;
                break;
            }
        }
    }
    if (missed_alarm_pending)
    {
        missed_alarm_pending = false;
        PushNotify_Trigger_Custom(sysConfig.alarms[pending_alarm_idx].prescript.c_str(), false);
    }
}

class AppAlarmEdit : public AppBase
{
    int h, m, phase;

    DialAnimator dialAnim;
    TacticalLinkEngine linkAnim;

    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 2)
        {
            const char *title = zh ? "危险操作" : "DANGER";
            HAL_Screen_ShowChineseLine(10, 26, title);

            char buf[64];
            sprintf(buf, zh ? "抹除 [%s] ?" : "DEL [%s] ?", sysConfig.alarms[g_alarm_edit_idx].name.c_str());
            int txt_w = HAL_Get_Text_Width(buf);
            HAL_Screen_ShowChineseLine(sw - txt_w - 10, 26, buf);

            const char *tip = zh ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        else
        {
            // ==========================================
            // 【UI 调优】：所有坐标整体上移！
            // ==========================================
            const char *names_zh[] = {"设定小时", "设定分钟"};
            const char *names_en[] = {"SET HR", "SET MIN"};
            const char **names = zh ? names_zh : names_en;

            // 1. 顶部战术链路：Y坐标从 6 调高到了 2
            linkAnim.draw(2, names, 2, phase, 120);

            // 2. 中间机甲分隔线：Y坐标从 24 调高到了 18
            int line_y = 18;
            HAL_Draw_Line(0, line_y, sw / 2 - 30, line_y, 1);
            HAL_Draw_Line(sw / 2 - 30, line_y, sw / 2 - 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 - 25, line_y + 3, sw / 2 + 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 + 25, line_y + 3, sw / 2 + 30, line_y, 1);
            HAL_Draw_Line(sw / 2 + 30, line_y, sw, line_y, 1);

            // 3. 底部机械刻度盘：Y坐标从 36 调高到了 28
            if (phase == 0)
                dialAnim.drawNumberDial(28, h, 0, 23, "");
            else
                dialAnim.drawNumberDial(28, m, 0, 59, "");

            // 4. 操作提示停留在底部：Y坐标 56
            const char *tip;
            if (phase == 0)
            {
                if (g_alarm_edit_idx >= 0)
                    tip = zh ? "长按删此闹钟 / 单击确认" : "LONG: DELETE / CLICK: NEXT";
                else
                    tip = zh ? "长按取消新建 / 单击确认" : "LONG: CANCEL / CLICK: NEXT";
            }
            else
                tip = zh ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";

            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        if (g_alarm_edit_idx >= 0)
        {
            h = sysConfig.alarms[g_alarm_edit_idx].hour;
            m = sysConfig.alarms[g_alarm_edit_idx].min;
        }
        else
        {
            h = 8;
            m = 0;
        }

        phase = 0;
        linkAnim.jumpTo(phase);
        drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override
    {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if (d_anim || l_anim)
        {
            drawUI();
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (phase == 2)
            return;

        if (phase == 0)
        {
            h += delta;
            if (h < 0)
                h = 23;
            if (h > 23)
                h = 0;
        }
        else
        {
            m += delta;
            if (m < 0)
                m = 59;
            if (m > 59)
                m = 0;
        }

        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        if (phase == 2)
        {
            phase = 0;
            linkAnim.jumpTo(phase);
            drawUI();
        }
        else if (phase == 0)
        {
            phase = 1;
            drawUI();
        }
        else
        {
            if (g_alarm_edit_idx >= 0)
            {
                sysConfig.alarms[g_alarm_edit_idx].hour = h;
                sysConfig.alarms[g_alarm_edit_idx].min = m;
            }
            else
            {
                // 这是本地新建闹钟的逻辑
                int idx = sysConfig.alarm_count;
                sysConfig.alarms[idx].hour = h;
                sysConfig.alarms[idx].min = m;
                sysConfig.alarms[idx].is_active = true;

                // ==========================================
                // 【核心修复】：动态生成 Alarm1, Alarm2 这样的名字
                // ==========================================
                char autoName[16];
                sprintf(autoName, "Alarm%d", idx + 1);
                sysConfig.alarms[idx].name = autoName;

                sysConfig.alarms[idx].prescript = "SYSTEM OVERRIDE. WAKE UP NOW.";
                sysConfig.alarm_count++;
            }
            // 记得保留原有的这行保存代码
            sysConfig.save();
            appManager.popApp();
        }
    }

    void onKeyLong() override
    {
        if (phase == 2)
        {
            SYS_SOUND_GLITCH();
            for (int i = g_alarm_edit_idx; i < sysConfig.alarm_count - 1; i++)
                sysConfig.alarms[i] = sysConfig.alarms[i + 1];
            sysConfig.alarm_count--;
            sysConfig.save();
            appManager.popApp();
        }
        else if (phase == 1)
        {
            SYS_SOUND_NAV();
            phase = 0;
            drawUI();
        }
        else
        {
            SYS_SOUND_NAV();
            if (g_alarm_edit_idx >= 0)
            {
                phase = 2;
                drawUI();
            }
            else
                appManager.popApp();
        }
    }
};
AppAlarmEdit instanceAlarmEdit;
AppBase *appAlarmEdit = &instanceAlarmEdit;

class AppAlarmMenu : public AppMenuBase
{
protected:
    int getMenuCount() override { return sysConfig.alarm_count + 2; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? "都市唤醒闹钟" : "WAKEUP ALARM"; }
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        static char buf[64];
        if (index == sysConfig.alarm_count)
            return zh ? " + 添加新闹钟" : " + ADD NEW ALARM";
        if (index == sysConfig.alarm_count + 1)
            return zh ? "返回主菜单" : "BACK TO MAIN MENU";

        AlarmPreset &a = sysConfig.alarms[index];
        const char *mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d:%02d [%s] %s%s", a.hour, a.min, a.name.c_str(), a.is_active ? "ON" : "OFF", mark);
        return buf;
    }
    void onItemClicked(int index) override
    {
        if (index == sysConfig.alarm_count)
        {
            if (sysConfig.alarm_count < 10)
            {
                g_alarm_edit_idx = -1;
                appManager.pushApp(appAlarmEdit);
            }
        }
        else if (index == sysConfig.alarm_count + 1)
        {
            appManager.popApp();
        }
        else
        {
            sysConfig.alarms[index].is_active = !sysConfig.alarms[index].is_active;
            sysConfig.save();
            drawMenuUI(visual_selection);
        }
    }
    void onLongPressed() override
    {
        if (current_selection < sysConfig.alarm_count)
        {
            g_alarm_edit_idx = current_selection;
            appManager.pushApp(appAlarmEdit);
        }
        else
            appManager.popApp();
    }

public:
    void onResume() override
    {
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }
};
AppAlarmMenu instanceAlarmMenu;
AppBase *appAlarm = &instanceAlarmMenu;