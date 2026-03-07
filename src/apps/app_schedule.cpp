// 文件：src/apps/app_schedule.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"
#include <time.h>

int g_schedule_edit_idx = -1; // -1:新建, >=0:修改或恢复
extern AppBase* appScheduleEdit;
extern AppBase* appScheduleExpired;

// 文件：src/apps/app_schedule.cpp (在文件顶部添加)



// 【新增】：根据标题遍历并抹除未过期的日程
void Schedule_DeleteMobile(const char* title) {
    bool deleted = false;
    for (int i = 0; i < sysConfig.schedule_count; i++) {
        // 只删除标题匹配的日程（不管过没过期直接清理）
        if (sysConfig.schedules[i].title == title) {
            for (int j = i; j < sysConfig.schedule_count - 1; j++) {
                sysConfig.schedules[j] = sysConfig.schedules[j+1];
            }
            sysConfig.schedule_count--;
            deleted = true;
            i--; 
        }
    }
    if (deleted) sysConfig.save();
}

// 【多语言适配】：提取动态预设语句
const char* Get_Title_Preset(int idx) {
    const char* zh_t[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
    const char* en_t[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
    return (appManager.getLanguage() == LANG_ZH) ? zh_t[idx] : en_t[idx];
}
const char* Get_Text_Preset(int p_idx) {
    const char* zh_p[] = {"", "日程时间已到，请立即执行。"};
    const char* en_p[] = {"", "SCHEDULE TIME REACHED. EXECUTE NOW."};
    return (appManager.getLanguage() == LANG_ZH) ? zh_p[p_idx] : en_p[p_idx];
}

void Schedule_AddMobile(uint32_t target_time, const char* title, const char* text) {
    if (sysConfig.schedule_count >= 15) return;
    int idx = sysConfig.schedule_count;
    sysConfig.schedules[idx].target_time = target_time;
    sysConfig.schedules[idx].title = title;
    sysConfig.schedules[idx].prescript = text;
    sysConfig.schedules[idx].is_expired = false;
    sysConfig.schedules[idx].is_restored = false;
    sysConfig.schedule_count++;
    sysConfig.save();
}

void Sort_Schedules() {
    for (int i = 0; i < sysConfig.schedule_count - 1; i++) {
        for (int j = 0; j < sysConfig.schedule_count - i - 1; j++) {
            if (sysConfig.schedules[j].target_time > sysConfig.schedules[j+1].target_time) {
                ScheduleItem temp = sysConfig.schedules[j];
                sysConfig.schedules[j] = sysConfig.schedules[j+1];
                sysConfig.schedules[j+1] = temp;
            }
        }
    }
}

void Schedule_UpdateBackground() {
    static uint32_t last_check = 0;
    if (millis() - last_check < 1000) return;
    last_check = millis();
    time_t now; time(&now);
    if (now < 1000000000) return; 
    

    bool need_save = false;
    for (int i = 0; i < sysConfig.schedule_count; i++) {
        ScheduleItem& s = sysConfig.schedules[i];
        if (s.is_expired && (now - s.expire_time > 86400)) {
            for (int j = i; j < sysConfig.schedule_count - 1; j++) sysConfig.schedules[j] = sysConfig.schedules[j+1];
            sysConfig.schedule_count--;
            need_save = true; i--; continue;
        }
        if (!s.is_expired && now >= s.target_time) {
            s.is_expired = true; s.expire_time = now; s.is_restored = false; need_save = true;
            if (s.prescript.length() == 0) PushNotify_Trigger_Random(false);
            else PushNotify_Trigger_Custom(s.prescript.c_str(), false);
        }
    }
    if (need_save) sysConfig.save();
}

class AppScheduleEdit : public AppBase {
    int mo, d, h, m, t_idx, p_idx, phase;
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width(); int sh = HAL_Get_Screen_Height();
        bool zh = appManager.getLanguage() == LANG_ZH;
        
        if (phase == 6) {
            HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, zh ? "危险操作验证" : "DANGER ZONE");
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);
            char buf[64]; sprintf(buf, zh ? "抹除日程 [%s] ?" : "DELETE [%s] ?", sysConfig.schedules[g_schedule_edit_idx].title.c_str());
            HAL_Screen_ShowChineseLine((sw - HAL_Get_Text_Width(buf))/2, UI_HEADER_HEIGHT + 30, buf);
            const char* tip = zh ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        } else {
            const char* ph_names_zh[] = {"月", "日", "时", "分", "标题类型", "执行动作"};
            const char* ph_names_en[] = {"MON", "DAY", "HR", "MIN", "TYPE", "ACTION"};
            char title_buf[64];
            const char* mode_title = (g_schedule_edit_idx >= 0 && !sysConfig.schedules[g_schedule_edit_idx].is_expired) ? (zh ? "修改日程" : "EDIT SCH") : (zh ? "新建/恢复" : "NEW/RESTORE");
            sprintf(title_buf, "%s [%s]", mode_title, zh ? ph_names_zh[phase] : ph_names_en[phase]);
            HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title_buf);
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

            int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2;
            char pref[32], val[32], suff[32];

            if (phase < 4) {
                if (phase==0) { strcpy(pref,"[ "); sprintf(val,"%02d",mo); sprintf(suff, zh ? " ]月%02d日" : " ]/%02d", d); }
                if (phase==1) { sprintf(pref, zh ? "%02d月[ " : "%02d/[ ", mo); sprintf(val,"%02d",d); strcpy(suff, zh ? " ]日" : " ]"); }
                if (phase==2) { strcpy(pref,"[ "); sprintf(val,"%02d",h); sprintf(suff," ] : %02d",m); }
                if (phase==3) { sprintf(pref,"%02d : [ ",h); sprintf(val,"%02d",m); strcpy(suff," ]"); }
            } else if (phase == 4) {
                strcpy(pref, "< "); strcpy(val, Get_Title_Preset(t_idx)); strcpy(suff, " >");
            } else if (phase == 5) {
                strcpy(pref, "< "); strcpy(val, (p_idx == 0) ? (zh ? "随机都市指令" : "RANDOM PRESCRIPT") : (zh ? "固定提醒语" : "FIXED ALERT")); strcpy(suff, " >");
            }
            int tw = HAL_Get_Text_Width(pref) + HAL_Get_Text_Width(val) + HAL_Get_Text_Width(suff);
            drawSegmentedAnimatedText((sw - tw)/2, center_y - 8, pref, val, suff, 0.0f);

            const char* tip = zh ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
            if (phase == 0) {
                if (g_schedule_edit_idx >= 0) {
                    if (!sysConfig.schedules[g_schedule_edit_idx].is_expired) tip = zh ? "长按删此日程 / 单击下一步" : "LONG: DELETE / CLICK: NEXT";
                    else tip = zh ? "长按取消恢复 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
                } else tip = zh ? "长按取消新建 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
            } else if (phase == 5) tip = zh ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";
            
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        }
        HAL_Screen_Update();
    }
public:
    void onCreate() override {
        time_t now; time(&now); struct tm t_info; localtime_r(&now, &t_info);
        if (g_schedule_edit_idx >= 0) {
            time_t tt = sysConfig.schedules[g_schedule_edit_idx].target_time;
            if (sysConfig.schedules[g_schedule_edit_idx].is_expired) tt = now; 
            struct tm s_info; localtime_r(&tt, &s_info);
            mo = s_info.tm_mon + 1; d = s_info.tm_mday; h = s_info.tm_hour; m = s_info.tm_min;
            t_idx = 0; p_idx = 0;
        } else {
            mo = t_info.tm_mon + 1; d = t_info.tm_mday; h = t_info.tm_hour; m = t_info.tm_min;
            t_idx = 0; p_idx = 0;
        }
        phase = 0; drawUI();
    }
    void onResume() override { drawUI(); }
    void onLoop() override { if(updateEditAnimation()) drawUI(); }
    void onDestroy() override {}
    void onKnob(int delta) override {
        if (phase == 6) return;
        if(phase==0) { mo += delta; if(mo<1) mo=12; if(mo>12) mo=1; }
        if(phase==1) { d += delta; if(d<1) d=31; if(d>31) d=1; }
        if(phase==2) { h += delta; if(h<0) h=23; if(h>23) h=0; }
        if(phase==3) { m += delta; if(m<0) m=59; if(m>59) m=0; }
        if(phase==4) { t_idx += delta; if(t_idx<0) t_idx=3; if(t_idx>3) t_idx=0; }
        if(phase==5) { p_idx += delta; if(p_idx<0) p_idx=1; if(p_idx>1) p_idx=0; }
        triggerEditAnimation(delta); SYS_SOUND_GLITCH(); drawUI();
    }
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 6) { phase = 0; drawUI(); } 
        else if (phase < 5) { phase++; drawUI(); } 
        else {
            time_t now; time(&now); struct tm t_info; localtime_r(&now, &t_info);
            t_info.tm_mon = mo - 1; t_info.tm_mday = d; t_info.tm_hour = h; t_info.tm_min = m; t_info.tm_sec = 0;
            time_t new_target = mktime(&t_info);
            if (new_target < now) { t_info.tm_year += 1; new_target = mktime(&t_info); }

            if (g_schedule_edit_idx >= 0) {
                sysConfig.schedules[g_schedule_edit_idx].target_time = new_target;
                sysConfig.schedules[g_schedule_edit_idx].title = Get_Title_Preset(t_idx);
                sysConfig.schedules[g_schedule_edit_idx].prescript = Get_Text_Preset(p_idx);
                if (sysConfig.schedules[g_schedule_edit_idx].is_expired) {
                    sysConfig.schedules[g_schedule_edit_idx].is_expired = false;
                    sysConfig.schedules[g_schedule_edit_idx].is_restored = true;
                }
            } else {
                int idx = sysConfig.schedule_count;
                sysConfig.schedules[idx].target_time = new_target;
                sysConfig.schedules[idx].title = Get_Title_Preset(t_idx);
                sysConfig.schedules[idx].prescript = Get_Text_Preset(p_idx);
                sysConfig.schedules[idx].is_expired = false;
                sysConfig.schedules[idx].is_restored = false;
                sysConfig.schedule_count++;
            }
            Sort_Schedules(); sysConfig.save(); appManager.popApp();
        }
    }
    void onKeyLong() override { 
        if (phase == 6) {
            SYS_SOUND_GLITCH();
            for(int i = g_schedule_edit_idx; i < sysConfig.schedule_count - 1; i++) sysConfig.schedules[i] = sysConfig.schedules[i+1];
            sysConfig.schedule_count--; sysConfig.save(); appManager.popApp(); 
        } else if (phase > 0) { SYS_SOUND_NAV(); phase--; drawUI(); } 
        else {
            SYS_SOUND_NAV(); 
            if (g_schedule_edit_idx >= 0 && !sysConfig.schedules[g_schedule_edit_idx].is_expired) { phase = 6; drawUI(); } 
            else { appManager.popApp(); }
        }
    }
};
AppScheduleEdit instanceScheduleEdit; AppBase* appScheduleEdit = &instanceScheduleEdit;

class AppScheduleExpired : public AppMenuBase {
    int expired_indices[15]; int expired_count;
protected:
    int getMenuCount() override { return expired_count + 1; }
    const char* getTitle() override { return appManager.getLanguage() == LANG_ZH ? "已过期日程收容所" : "EXPIRED ARCHIVE"; }
    const char* getItemText(int index) override {
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == expired_count) return zh ? "返回上一级" : "BACK TO SCH";
        int real_i = expired_indices[index]; ScheduleItem& s = sysConfig.schedules[real_i];
        struct tm t_info; time_t tt = s.target_time; localtime_r(&tt, &t_info);
        static char buf[64]; const char* mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d/%02d %02d:%02d %s %s%s", t_info.tm_mon+1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min, s.title.c_str(), zh ? "(过期)" : "(EXP)", mark);
        return buf;
    }
    void onItemClicked(int index) override { appManager.popApp(); }
    void onLongPressed() override {
        if (current_selection < expired_count) { g_schedule_edit_idx = expired_indices[current_selection]; appManager.pushApp(appScheduleEdit); } 
        else appManager.popApp();
    }
public:
    void onResume() override { 
        expired_count = 0;
        for(int i=0; i<sysConfig.schedule_count; i++) if(sysConfig.schedules[i].is_expired) expired_indices[expired_count++] = i;
        if (current_selection >= getMenuCount()) current_selection = getMenuCount() - 1;
        if (current_selection < 0) current_selection = 0;
        AppMenuBase::onResume(); 
    }
};
AppScheduleExpired instanceScheduleExpired; AppBase* appScheduleExpired = &instanceScheduleExpired;

class AppScheduleMenu : public AppMenuBase {
    int active_indices[15]; int active_count;
protected:
    int getMenuCount() override { return active_count + 3; }
    const char* getTitle() override { return appManager.getLanguage() == LANG_ZH ? "都市日程计划" : "SCHEDULES"; }
    uint16_t getItemColor(int index) override {
        if (index == 0 || index >= active_count + 1) return TFT_CYAN;
        int real_i = active_indices[index - 1];
        if (sysConfig.schedules[real_i].is_restored) return TFT_RED; 
        return TFT_CYAN;
    }
    const char* getItemText(int index) override {
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0) return zh ? "[ 打开已过期日程库 ]" : "[ EXPIRED ARCHIVE ]";
        if (index == active_count + 1) return zh ? " + 登记新日程" : " + ADD SCHEDULE";
        if (index == active_count + 2) return zh ? "返回主菜单" : "BACK TO MAIN MENU";

        int real_i = active_indices[index - 1]; ScheduleItem& s = sysConfig.schedules[real_i];
        struct tm t_info; time_t tt = s.target_time; localtime_r(&tt, &t_info);
        static char buf[64]; const char* mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d/%02d %02d:%02d %s%s", t_info.tm_mon+1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min, s.title.c_str(), mark);
        return buf;
    }
    void onItemClicked(int index) override {
        if (index == 0) appManager.pushApp(appScheduleExpired);
        else if (index == active_count + 1) { g_schedule_edit_idx = -1; appManager.pushApp(appScheduleEdit); }
        else if (index == active_count + 2) appManager.popApp();
    }
    void onLongPressed() override {
        if (current_selection > 0 && current_selection <= active_count) {
            g_schedule_edit_idx = active_indices[current_selection - 1]; appManager.pushApp(appScheduleEdit);
        } else appManager.popApp();
    }
public:
    void onResume() override { 
        active_count = 0;
        for(int i=0; i<sysConfig.schedule_count; i++) if(!sysConfig.schedules[i].is_expired) active_indices[active_count++] = i;
        if (current_selection >= getMenuCount()) current_selection = getMenuCount() - 1;
        if (current_selection < 0) current_selection = 0;
        AppMenuBase::onResume(); 
    }
};
AppScheduleMenu instanceScheduleMenu; AppBase* appSchedule = &instanceScheduleMenu;