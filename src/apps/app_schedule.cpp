// 文件：src/apps/app_schedule.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"
#include <time.h>

int g_schedule_edit_idx = -1;
extern AppBase *appScheduleEdit;
extern AppBase *appScheduleExpired;

void Schedule_DeleteMobile(const char *title)
{
    bool deleted = false;
    for (int i = 0; i < sysConfig.schedule_count; i++)
    {
        if (sysConfig.schedules[i].title == title)
        {
            for (int j = i; j < sysConfig.schedule_count - 1; j++)
            {
                sysConfig.schedules[j] = sysConfig.schedules[j + 1];
            }
            sysConfig.schedule_count--;
            deleted = true;
            i--;
        }
    }
    if (deleted)
        sysConfig.save();
}

const char *Get_Title_Preset(int idx)
{
    const char *zh_t[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
    const char *en_t[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
    return (appManager.getLanguage() == LANG_ZH) ? zh_t[idx] : en_t[idx];
}
const char *Get_Text_Preset(int p_idx)
{
    const char *zh_p[] = {"", "日程时间已到，请立即执行。"};
    const char *en_p[] = {"", "SCHEDULE TIME REACHED. EXECUTE NOW."};
    return (appManager.getLanguage() == LANG_ZH) ? zh_p[p_idx] : en_p[p_idx];
}

void Sort_Schedules()
{
    for (int i = 0; i < sysConfig.schedule_count - 1; i++)
    {
        for (int j = 0; j < sysConfig.schedule_count - i - 1; j++)
        {
            if (sysConfig.schedules[j].target_time > sysConfig.schedules[j + 1].target_time)
            {
                ScheduleItem temp = sysConfig.schedules[j];
                sysConfig.schedules[j] = sysConfig.schedules[j + 1];
                sysConfig.schedules[j + 1] = temp;
            }
        }
    }
}

// 【修改】：末尾增加 bool is_hidden = false 参数
void Schedule_AddMobile(uint32_t target_time, const char *title, const char *text, bool is_hidden)
{
    if (sysConfig.schedule_count >= 15)
        return;
    int idx = sysConfig.schedule_count;
    sysConfig.schedules[idx].target_time = target_time;
    sysConfig.schedules[idx].title = title;
    sysConfig.schedules[idx].prescript = text;
    sysConfig.schedules[idx].is_expired = false;
    sysConfig.schedules[idx].is_restored = false;
    sysConfig.schedules[idx].is_hidden = is_hidden; // 【新增】：记录隐藏状态
    sysConfig.schedule_count++;
    Sort_Schedules();
    sysConfig.save();
}

void Schedule_UpdateBackground()
{
    static uint32_t last_check = 0;
    if (millis() - last_check < 1000)
        return;
    last_check = millis();
    time_t now;
    time(&now);
    if (now < 1000000000)
        return;

    bool need_save = false;
    for (int i = 0; i < sysConfig.schedule_count; i++)
    {
        ScheduleItem &s = sysConfig.schedules[i];
        if (s.is_expired && (now - s.expire_time > 86400))
        {
            for (int j = i; j < sysConfig.schedule_count - 1; j++)
                sysConfig.schedules[j] = sysConfig.schedules[j + 1];
            sysConfig.schedule_count--;
            need_save = true;
            i--;
            continue;
        }
        if (!s.is_expired && now >= s.target_time)
        {
            s.is_expired = true;
            s.expire_time = now;
            s.is_restored = false;
            need_save = true;
            if (s.prescript.length() == 0)
                PushNotify_Trigger_Random(false);
            else
                PushNotify_Trigger_Custom(s.prescript.c_str(), false);
        }
    }
    if (need_save)
        sysConfig.save();
}

class AppScheduleEdit : public AppBase
{
    int mo, d, h, m, t_idx, p_idx, phase;

    DialAnimator dialAnim;       // 实例化刻度盘引擎
    TacticalLinkEngine linkAnim; // 实例化战术链路引擎

    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 6)
        {
            const char *title = zh ? "危险操作" : "DANGER";
            HAL_Screen_ShowChineseLine(10, 26, title);

            char buf[64];
            sprintf(buf, zh ? "抹除 [%s]?" : "DEL [%s]?", sysConfig.schedules[g_schedule_edit_idx].title.c_str());
            int txt_w = HAL_Get_Text_Width(buf);
            HAL_Screen_ShowChineseLine(sw - txt_w - 10, 26, buf);

            const char *tip = zh ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        else
        {
            // 1. 顶部战术链路区
            const char *names_zh[] = {"设定月份", "设定日期", "设定小时", "设定分钟", "选择类型", "执行动作"};
            const char *names_en[] = {"SET MON", "SET DAY", "SET HR", "SET MIN", "SCH TYPE", "ACTION"};
            const char **names = zh ? names_zh : names_en;

            // 节点较多，间距设为 95
            linkAnim.draw(2, names, 6, phase, 95);

            // 2. 中间机甲分隔线
            int line_y = 18;
            HAL_Draw_Line(0, line_y, sw / 2 - 30, line_y, 1);
            HAL_Draw_Line(sw / 2 - 30, line_y, sw / 2 - 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 - 25, line_y + 3, sw / 2 + 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 + 25, line_y + 3, sw / 2 + 30, line_y, 1);
            HAL_Draw_Line(sw / 2 + 30, line_y, sw, line_y, 1);

            // 3. 底部动态机械刻度盘
            if (phase == 0)
                dialAnim.drawNumberDial(28, mo, 1, 12, "");
            else if (phase == 1)
                dialAnim.drawNumberDial(28, d, 1, 31, "");
            else if (phase == 2)
                dialAnim.drawNumberDial(28, h, 0, 23, "");
            else if (phase == 3)
                dialAnim.drawNumberDial(28, m, 0, 59, "");
            else if (phase == 4)
            {
                const char *t_zh[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
                const char *t_en[] = {"ROUTINE", "MEETING", "MAINTAIN", "EMERGENCY"};
                dialAnim.drawStringDial(28, t_idx, zh ? t_zh : t_en, 4);
            }
            else if (phase == 5)
            {
                const char *p_zh[] = {"随机指令", "固定提醒"};
                const char *p_en[] = {"RANDOM", "FIXED MSG"};
                dialAnim.drawStringDial(28, p_idx, zh ? p_zh : p_en, 2);
            }

            // 4. 底部状态与操作指引
            const char *tip = zh ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
            if (phase == 0)
            {
                if (g_schedule_edit_idx >= 0)
                {
                    if (!sysConfig.schedules[g_schedule_edit_idx].is_expired)
                        tip = zh ? "长按删此日程 / 单击下一步" : "LONG: DELETE / CLICK: NEXT";
                    else
                        tip = zh ? "长按取消恢复 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
                }
                else
                    tip = zh ? "长按取消新建 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
            }
            else if (phase == 5)
                tip = zh ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";

            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        time_t now;
        time(&now);
        struct tm t_info;
        localtime_r(&now, &t_info);
        if (g_schedule_edit_idx >= 0)
        {
            time_t tt = sysConfig.schedules[g_schedule_edit_idx].target_time;
            if (sysConfig.schedules[g_schedule_edit_idx].is_expired)
                tt = now;
            struct tm s_info;
            localtime_r(&tt, &s_info);
            mo = s_info.tm_mon + 1;
            d = s_info.tm_mday;
            h = s_info.tm_hour;
            m = s_info.tm_min;
            t_idx = 0;
            p_idx = 0;
        }
        else
        {
            mo = t_info.tm_mon + 1;
            d = t_info.tm_mday;
            h = t_info.tm_hour;
            m = t_info.tm_min;
            t_idx = 0;
            p_idx = 0;
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
            drawUI();
    }

    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (phase == 6)
            return;
        if (phase == 0)
        {
            mo += delta;
            if (mo < 1)
                mo = 12;
            if (mo > 12)
                mo = 1;
        }
        if (phase == 1)
        {
            d += delta;
            if (d < 1)
                d = 31;
            if (d > 31)
                d = 1;
        }
        if (phase == 2)
        {
            h += delta;
            if (h < 0)
                h = 23;
            if (h > 23)
                h = 0;
        }
        if (phase == 3)
        {
            m += delta;
            if (m < 0)
                m = 59;
            if (m > 59)
                m = 0;
        }
        if (phase == 4)
        {
            t_idx += delta;
            if (t_idx < 0)
                t_idx = 3;
            if (t_idx > 3)
                t_idx = 0;
        }
        if (phase == 5)
        {
            p_idx += delta;
            if (p_idx < 0)
                p_idx = 1;
            if (p_idx > 1)
                p_idx = 0;
        }

        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        if (phase == 6)
        {
            phase = 0;
            linkAnim.jumpTo(phase);
            drawUI();
        }
        else if (phase < 5)
        {
            phase++;
            drawUI();
        }
        else
        {
            time_t now;
            time(&now);
            struct tm t_info;
            localtime_r(&now, &t_info);
            t_info.tm_mon = mo - 1;
            t_info.tm_mday = d;
            t_info.tm_hour = h;
            t_info.tm_min = m;
            t_info.tm_sec = 0;
            time_t new_target = mktime(&t_info);
            if (new_target < now)
            {
                t_info.tm_year += 1;
                new_target = mktime(&t_info);
            }

            if (g_schedule_edit_idx >= 0)
            {
                sysConfig.schedules[g_schedule_edit_idx].target_time = new_target;
                sysConfig.schedules[g_schedule_edit_idx].title = Get_Title_Preset(t_idx);
                sysConfig.schedules[g_schedule_edit_idx].prescript = Get_Text_Preset(p_idx);
                if (sysConfig.schedules[g_schedule_edit_idx].is_expired)
                {
                    sysConfig.schedules[g_schedule_edit_idx].is_expired = false;
                    sysConfig.schedules[g_schedule_edit_idx].is_restored = true;
                }
            }
            else
            {
                int idx = sysConfig.schedule_count;
                sysConfig.schedules[idx].target_time = new_target;
                sysConfig.schedules[idx].title = Get_Title_Preset(t_idx);
                sysConfig.schedules[idx].prescript = Get_Text_Preset(p_idx);
                sysConfig.schedules[idx].is_expired = false;
                sysConfig.schedules[idx].is_restored = false;
                sysConfig.schedules[idx].is_hidden = false; // 【新增】：本机手工新建绝对不隐藏
                sysConfig.schedule_count++;
            }
            Sort_Schedules();
            sysConfig.save();
            appManager.popApp();
        }
    }

    void onKeyLong() override
    {
        if (phase == 6)
        {
            SYS_SOUND_GLITCH();
            for (int i = g_schedule_edit_idx; i < sysConfig.schedule_count - 1; i++)
                sysConfig.schedules[i] = sysConfig.schedules[i + 1];
            sysConfig.schedule_count--;
            sysConfig.save();
            appManager.popApp();
        }
        else if (phase > 0)
        {
            SYS_SOUND_NAV();
            phase--;
            drawUI();
        }
        else
        {
            SYS_SOUND_NAV();
            if (g_schedule_edit_idx >= 0 && !sysConfig.schedules[g_schedule_edit_idx].is_expired)
            {
                phase = 6;
                drawUI();
            }
            else
            {
                appManager.popApp();
            }
        }
    }
};
AppScheduleEdit instanceScheduleEdit;
AppBase *appScheduleEdit = &instanceScheduleEdit;

class AppScheduleExpired : public AppMenuBase
{
    int expired_indices[15];
    int expired_count;

protected:
    int getMenuCount() override { return expired_count + 1; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? "已过期日程收容所" : "EXPIRED ARCHIVE"; }
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == expired_count)
            return zh ? "返回上一级" : "BACK TO SCH";
        int real_i = expired_indices[index];
        ScheduleItem &s = sysConfig.schedules[real_i];
        struct tm t_info;
        time_t tt = s.target_time;
        localtime_r(&tt, &t_info);
        static char buf[64];
        const char *mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d/%02d %02d:%02d %s %s%s", t_info.tm_mon + 1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min, s.title.c_str(), zh ? "(过期)" : "(EXP)", mark);
        return buf;
    }
    void onItemClicked(int index) override
    {
        if (index < expired_count)
        {
            g_schedule_edit_idx = expired_indices[index];
            appManager.pushApp(appScheduleEdit);
        }
        else
            appManager.popApp();
    }
    void onLongPressed() override { appManager.popApp(); }

public:
    void onResume() override
    {
        expired_count = 0;
        for (int i = 0; i < sysConfig.schedule_count; i++)
            // 【修改】：并且不是隐藏日程，才允许装载进 UI 列表
            if (sysConfig.schedules[i].is_expired && !sysConfig.schedules[i].is_hidden)
                expired_indices[expired_count++] = i;
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }
};
AppScheduleExpired instanceScheduleExpired;
AppBase *appScheduleExpired = &instanceScheduleExpired;

class AppScheduleMenu : public AppMenuBase
{
    int active_indices[15];
    int active_count;

protected:
    int getMenuCount() override { return active_count + 3; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? "都市日程计划" : "SCHEDULES"; }
    uint16_t getItemColor(int index) override
    {
        if (index == 0 || index >= active_count + 1)
            return TFT_CYAN;
        int real_i = active_indices[index - 1];
        if (sysConfig.schedules[real_i].is_restored)
            return TFT_RED;
        return TFT_CYAN;
    }
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0)
            return zh ? "[ 打开已过期日程库 ]" : "[ EXPIRED ARCHIVE ]";
        if (index == active_count + 1)
            return zh ? " + 登记新日程" : " + ADD SCHEDULE";
        if (index == active_count + 2)
            return zh ? "返回主菜单" : "BACK TO MAIN MENU";

        int real_i = active_indices[index - 1];
        ScheduleItem &s = sysConfig.schedules[real_i];
        struct tm t_info;
        time_t tt = s.target_time;
        localtime_r(&tt, &t_info);
        static char buf[64];
        const char *mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d/%02d %02d:%02d %s%s", t_info.tm_mon + 1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min, s.title.c_str(), mark);
        return buf;
    }
    void onItemClicked(int index) override
    {
        if (index == 0)
        {
            appManager.pushApp(appScheduleExpired);
        }
        else if (index > 0 && index <= active_count)
        {
            g_schedule_edit_idx = active_indices[index - 1];
            appManager.pushApp(appScheduleEdit);
        }
        else if (index == active_count + 1)
        {
            g_schedule_edit_idx = -1;
            appManager.pushApp(appScheduleEdit);
        }
        else if (index == active_count + 2)
        {
            appManager.popApp();
        }
    }
    void onLongPressed() override { appManager.popApp(); }

public:
    void onResume() override
    {
        active_count = 0;
        for (int i = 0; i < sysConfig.schedule_count; i++)
           if(!sysConfig.schedules[i].is_expired && !sysConfig.schedules[i].is_hidden) 
                active_indices[active_count++] = i;
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }
};
AppScheduleMenu instanceScheduleMenu;
AppBase *appSchedule = &instanceScheduleMenu;