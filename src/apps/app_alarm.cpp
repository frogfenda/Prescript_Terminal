// 文件：src/apps/app_alarm.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"

int g_alarm_delete_idx = 0;
extern AppBase* appAlarmDeleteConfirm;
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
    if (millis() - last_check < 1000) return; // 1秒检查一次
    last_check = millis();

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return; // 没联网对时则不触发

    static int last_trigger_min = -1;
    if (timeinfo.tm_min == last_trigger_min) return; // 一分钟内绝不重复触发

    for (int i = 0; i < sysConfig.alarm_count; i++) {
        if (sysConfig.alarms[i].is_active &&
            sysConfig.alarms[i].hour == timeinfo.tm_hour &&
            sysConfig.alarms[i].min == timeinfo.tm_min) {
            
            last_trigger_min = timeinfo.tm_min;

            // 剧烈震鸣警报！
            for(int b=0; b<5; b++) {
                HAL_Buzzer_Play_Tone(2500, 100);
                delay(50);
            }

            // 唤醒模式4：弹出它专属的文字，必须人工按下旋钮才解码！
            Prescript_Launch_Custom_Wait(sysConfig.alarms[i].prescript.c_str());
            appManager.launchApp(appPrescript); // 霸占屏幕！
            break;
        }
    }
}

// ---------------- 微模块 1: 删除确认 ----------------
class AppAlarmDeleteConfirm : public AppBase {
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, "危险操作验证");
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

        char buf[64];
        sprintf(buf, "彻底抹除 [%s] ?", sysConfig.alarms[g_alarm_delete_idx].name.c_str());
        HAL_Screen_ShowChineseLine((sw - HAL_Get_Text_Width(buf))/2, UI_HEADER_HEIGHT + 30, buf);

        const char* tip = "长按确认抹除 / 单击取消";
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        HAL_Screen_Update();
    }
public:
    void onCreate() override { drawUI(); }
    void onResume() override { drawUI(); }
    void onLoop() override {}
    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        appManager.popApp();
    }
    void onKeyLong() override {
        SYS_SOUND_GLITCH();
        // 数组平移覆盖
        for(int i = g_alarm_delete_idx; i < sysConfig.alarm_count - 1; i++){
            sysConfig.alarms[i] = sysConfig.alarms[i+1];
        }
        sysConfig.alarm_count--;
        sysConfig.save();
        appManager.popApp();
    }
};
AppAlarmDeleteConfirm instanceAlarmDeleteConfirm;
AppBase* appAlarmDeleteConfirm = &instanceAlarmDeleteConfirm;

// ---------------- 微模块 2: 添加设定 ----------------
class AppAlarmEdit : public AppBase {
    int h, m, phase;
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, "设定唤醒时间");
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

        int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2;

        char val_buf[16];
        const char* pref = (phase == 0) ? "时: " : "分: ";
        const char* suff = " <";
        sprintf(val_buf, "%02d", (phase == 0) ? h : m);

        int text_w = HAL_Get_Text_Width(pref) + HAL_Get_Text_Width(val_buf) + HAL_Get_Text_Width(suff);
        drawSegmentedAnimatedText((sw - text_w)/2, center_y - 8, pref, val_buf, suff, 0.0f);

        const char* tip = "长按取消 / 单击确认";
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        HAL_Screen_Update();
    }
public:
    void onCreate() override { h = 8; m = 0; phase = 0; drawUI(); }
    void onResume() override { drawUI(); }
    void onLoop() override { if(updateEditAnimation()) drawUI(); }
    void onDestroy() override {}
    void onKnob(int delta) override {
        if(phase==0) { h += delta; if(h<0) h=23; if(h>23) h=0; }
        else { m += delta; if(m<0) m=59; if(m>59) m=0; }
        triggerEditAnimation(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1; drawUI();
        } else {
            int idx = sysConfig.alarm_count;
            sysConfig.alarms[idx].hour = h;
            sysConfig.alarms[idx].min = m;
            sysConfig.alarms[idx].is_active = true;
            sysConfig.alarms[idx].name = "闹钟" + String(idx + 1);
            sysConfig.alarms[idx].prescript = "唤醒时间已到。执行强制终止休眠程序。"; // 默认语句
            sysConfig.alarm_count++;
            sysConfig.save();
            appManager.popApp();
        }
    }
    void onKeyLong() override { SYS_SOUND_NAV(); appManager.popApp(); }
};
AppAlarmEdit instanceAlarmEdit;
AppBase* appAlarmEdit = &instanceAlarmEdit;

// ---------------- 微模块 3: 闹钟3D主菜单 ----------------
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
            if (sysConfig.alarm_count < 10) appManager.pushApp(appAlarmEdit);
        } else if (index == sysConfig.alarm_count + 1) {
            appManager.popApp();
        } else {
            // 短按：启用/禁用
            sysConfig.alarms[index].is_active = !sysConfig.alarms[index].is_active;
            sysConfig.save();
            drawMenuUI(visual_selection);
        }
    }

    void onLongPressed() override {
        if (current_selection < sysConfig.alarm_count) {
            // 长按闹钟：呼出删除界面
            g_alarm_delete_idx = current_selection;
            appManager.pushApp(appAlarmDeleteConfirm);
        } else {
            appManager.popApp();
        }
    }
public:
    void onResume() override { AppMenuBase::onResume(); }
};
AppAlarmMenu instanceAlarmMenu;
AppBase* appAlarm = &instanceAlarmMenu;