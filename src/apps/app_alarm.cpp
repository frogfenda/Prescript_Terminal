// 文件：src/apps/app_alarm.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"

int g_alarm_edit_idx = -1; // -1表示新建，>=0表示修改

extern AppBase* appAlarmEdit;

// 【保留给手机蓝牙的终极接口】
void Alarm_AddPresetMobile(const char* name, int hour, int min, const char* text) {
    if (sysConfig.alarm_count >= 10) return;
    int idx = sysConfig.alarm_count;
    sysConfig.alarms[idx].hour = hour;
    sysConfig.alarms[idx].min = min;
    sysConfig.alarms[idx].is_active = true;
    sysConfig.alarms[idx].name = name;
    sysConfig.alarms[idx].prescript = text;
    sysConfig.alarm_count++;
    sysConfig.save();
}

// 【核心后台守护：时间检测触发】
void Alarm_UpdateBackground() {
    static uint32_t last_check = 0;
    if (millis() - last_check < 1000) return; 
    last_check = millis();

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return; 

    static int last_trigger_min = -1;
    static bool missed_alarm_pending = false; 
    static int pending_alarm_idx = -1;

    // 1. 抓取时间（无视系统是否繁忙，绝对不错过哪怕一秒）
    if (timeinfo.tm_min != last_trigger_min) {
        for (int i = 0; i < sysConfig.alarm_count; i++) {
            if (sysConfig.alarms[i].is_active &&
                sysConfig.alarms[i].hour == timeinfo.tm_hour &&
                sysConfig.alarms[i].min == timeinfo.tm_min) {
                
                missed_alarm_pending = true; // 把闹钟挂起在内存里
                pending_alarm_idx = i;
                last_trigger_min = timeinfo.tm_min;
                break; 
            }
        }
    }
        // 2. 伺机触发（只要系统一闲下来，立刻把欠下的警报弹出来！）
    if (missed_alarm_pending) {
        if (AppManagerLock::isSystemBusy(appManager.getCurrentApp())) return;

        missed_alarm_pending = false;
        PushNotify_Trigger_Custom(sysConfig.alarms[pending_alarm_idx].prescript.c_str(), false);
    }
}
// ---------------- 微模块 1: 闹钟参数设定 (绝妙融合新建、修改与删除) ----------------
class AppAlarmEdit : public AppBase {
    int h, m, phase; // phase 0:时, 1:分, 2:删除确认界面
    
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        
        // 【全新逻辑】：如果是 phase 2，直接在当前页面原地画出危险警告！
        if (phase == 2) {
            HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, "危险操作验证");
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

            char buf[64];
            sprintf(buf, "彻底抹除 [%s] ?", sysConfig.alarms[g_alarm_edit_idx].name.c_str());
            HAL_Screen_ShowChineseLine((sw - HAL_Get_Text_Width(buf))/2, UI_HEADER_HEIGHT + 30, buf);

            const char* tip = "长按确认抹除 / 单击返回编辑";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        } else {
            const char* action = (g_alarm_edit_idx >= 0) ? "修改闹钟" : "新建闹钟";
            char title_buf[64];
            sprintf(title_buf, "%s [%s]", action, (phase == 0) ? "时" : "分");
            HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title_buf);
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

            int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2;

            char pref[16], val_buf[16], suff[16];
            if (phase == 0) {
                strcpy(pref, "[ ");
                sprintf(val_buf, "%02d", h);
                sprintf(suff, " ] : %02d", m);
            } else {
                sprintf(pref, "%02d : [ ", h);
                sprintf(val_buf, "%02d", m);
                strcpy(suff, " ]");
            }

            int text_w = HAL_Get_Text_Width(pref) + HAL_Get_Text_Width(val_buf) + HAL_Get_Text_Width(suff);
            drawSegmentedAnimatedText((sw - text_w)/2, center_y - 8, pref, val_buf, suff, 0.0f);

            const char* tip;
            if (phase == 0) {
                tip = (g_alarm_edit_idx >= 0) ? "长按删除此闹钟 / 单击确认" : "长按取消新建 / 单击确认";
            } else {
                tip = "长按返回 / 单击保存";
            }
            
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        }
        HAL_Screen_Update();
    }
public:
    void onCreate() override { 
        if (g_alarm_edit_idx >= 0) {
            h = sysConfig.alarms[g_alarm_edit_idx].hour;
            m = sysConfig.alarms[g_alarm_edit_idx].min;
        } else {
            h = 8; m = 0; 
        }
        phase = 0; 
        drawUI(); 
    }
    void onResume() override { drawUI(); }
    void onLoop() override { if(updateEditAnimation()) drawUI(); }
    void onDestroy() override {}
    
    void onKnob(int delta) override {
        if (phase == 2) return; // 警告界面屏蔽旋钮操作
        
        if(phase==0) { h += delta; if(h<0) h=23; if(h>23) h=0; }
        else { m += delta; if(m<0) m=59; if(m>59) m=0; }
        triggerEditAnimation(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }
    
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 2) {
            // 【人性化回退】：不小心按到删除？单击就能无缝退回到时间修改状态！
            phase = 0;
            drawUI();
        } else if (phase == 0) {
            phase = 1; drawUI();
        } else {
            if (g_alarm_edit_idx >= 0) {
                sysConfig.alarms[g_alarm_edit_idx].hour = h;
                sysConfig.alarms[g_alarm_edit_idx].min = m;
            } else {
                int idx = sysConfig.alarm_count;
                sysConfig.alarms[idx].hour = h;
                sysConfig.alarms[idx].min = m;
                sysConfig.alarms[idx].is_active = true;
                sysConfig.alarms[idx].name = "闹钟" + String(idx + 1);
                sysConfig.alarms[idx].prescript = "唤醒时间已到。执行强制终止休眠程序。"; 
                sysConfig.alarm_count++;
            }
            sysConfig.save();
            appManager.popApp();
        }
    }
    
    void onKeyLong() override { 
        if (phase == 2) {
            SYS_SOUND_GLITCH();
            // 【执行刺杀】：彻底抹除闹钟
            for(int i = g_alarm_edit_idx; i < sysConfig.alarm_count - 1; i++){
                sysConfig.alarms[i] = sysConfig.alarms[i+1];
            }
            sysConfig.alarm_count--;
            sysConfig.save();
            appManager.popApp(); // 杀完直接退回主菜单
        } else if (phase == 1) {
            SYS_SOUND_NAV(); 
            phase = 0; 
            drawUI();
        } else {
            SYS_SOUND_NAV(); 
            if (g_alarm_edit_idx >= 0) {
                // 【进入刺杀模式】：原地变身成删除警告界面，毫无闪烁！
                phase = 2; 
                drawUI();
            } else {
                appManager.popApp(); 
            }
        }
    }
};
AppAlarmEdit instanceAlarmEdit;
AppBase* appAlarmEdit = &instanceAlarmEdit;


// ---------------- 微模块 2: 闹钟3D主菜单 ----------------
class AppAlarmMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return sysConfig.alarm_count + 2; }
    const char* getTitle() override { return "都市唤醒闹钟"; }
    
    const char* getItemText(int index) override {
        static char buf[64];
        if (index == sysConfig.alarm_count) return " + 添加新闹钟";
        if (index == sysConfig.alarm_count + 1) return "返回主菜单";

        AlarmPreset& a = sysConfig.alarms[index];
        const char* mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d:%02d [%s] %s%s", a.hour, a.min, a.name.c_str(), a.is_active ? "ON" : "OFF", mark);
        return buf;
    }

    void onItemClicked(int index) override {
        if (index == sysConfig.alarm_count) {
            if (sysConfig.alarm_count < 10) {
                g_alarm_edit_idx = -1; 
                appManager.pushApp(appAlarmEdit);
            }
        } else if (index == sysConfig.alarm_count + 1) {
            appManager.popApp();
        } else {
            sysConfig.alarms[index].is_active = !sysConfig.alarms[index].is_active;
            sysConfig.save();
            drawMenuUI(visual_selection);
        }
    }

    void onLongPressed() override {
        if (current_selection < sysConfig.alarm_count) {
            g_alarm_edit_idx = current_selection;
            appManager.pushApp(appAlarmEdit);
        } else {
            appManager.popApp();
        }
    }
public:
    void onResume() override { 
        // 斩断幽灵越界指针！
        if (current_selection >= getMenuCount()) current_selection = getMenuCount() - 1;
        if (current_selection < 0) current_selection = 0;
        AppMenuBase::onResume(); 
    }
};
AppAlarmMenu instanceAlarmMenu;
AppBase* appAlarm = &instanceAlarmMenu;