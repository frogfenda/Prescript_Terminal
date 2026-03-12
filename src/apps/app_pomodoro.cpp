// 文件：src/apps/app_pomodoro.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"

void Pomodoro_UpdatePreset(int index, const char* name, int work_m, int rest_m) {
    if (index < 0 || index >= 5) return;
    sysConfig.pomodoro_presets[index].name = name;
    sysConfig.pomodoro_presets[index].work_min = work_m;
    sysConfig.pomodoro_presets[index].rest_min = rest_m;
    sysConfig.save();
}

extern AppBase* appPomodoroRun;
extern AppBase* appPomodoroPresets;
extern AppBase* appPomodoroEdit;

class AppPomodoroRun : public AppBase {
private:
    int phase; 
    uint32_t timer_start;
    uint32_t current_duration;
    uint32_t last_sec_draw;
    bool is_paused;
    uint32_t pause_start_time;
    PomodoroPreset current_preset;

    // 【全新UI】：番茄钟执行界面无框、左右排版极简设计
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int center_y = 30;
        
        // 左侧：预设名称
        HAL_Screen_ShowChineseLine(10, center_y, current_preset.name.c_str());
        
        uint32_t elapsed = is_paused ? (pause_start_time - timer_start) : (millis() - timer_start);
        uint32_t remain = (current_duration > elapsed) ? (current_duration - elapsed) : 0;
        
        // 右侧：倒计时数字 (彻底去掉了之前的矩形边框！)
        char time_buf[16];
        sprintf(time_buf, "%02d:%02d", remain / 60000, (remain / 1000) % 60);
        int tw = HAL_Get_Text_Width(time_buf);
        HAL_Screen_ShowTextLine(sw - tw - 10, center_y, time_buf);
        
        // 底部居中：状态提示
        const char* state_str;
        if (appManager.getLanguage() == LANG_ZH) {
            if (is_paused) state_str = "已暂停 (单击继续 / 长按终止)";
            else state_str = (phase == 0) ? "正在执行专注..." : "正在休眠恢复...";
        } else {
            if (is_paused) state_str = "PAUSED (CLICK:RESUME/LONG:STOP)";
            else state_str = (phase == 0) ? "WORKING..." : "RESTING...";
        }
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(state_str))/2, 60, state_str, 0.4f);
        
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        current_preset = sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx];
        phase = 0; 
        is_paused = false;
        current_duration = current_preset.work_min * 60000; 
        timer_start = millis();
        last_sec_draw = 0xFFFFFFFF;
        drawUI();
    }
    
    void onResume() override { drawUI(); }

    void onLoop() override {
        if (!is_paused) {
            uint32_t elapsed = millis() - timer_start;
            if (elapsed >= current_duration) {
                if (phase == 0) {
                    phase = 1;
                    current_duration = current_preset.rest_min * 60000;
                    timer_start = millis();
                    is_paused = true; 
                    pause_start_time = millis();
                    
                    PushNotify_Trigger_Custom(appManager.getLanguage() == LANG_ZH ? 
                        "专注周期结束。立刻起身活动恢复精力。" : 
                        "WORK CYCLE COMPLETED. REST IMMEDIATELY.", true);
                } else {
                    appManager.popApp(); 
                    PushNotify_Trigger_Custom(appManager.getLanguage() == LANG_ZH ? 
                        "休眠恢复完毕。系统已重置，准备接受新的专注指令。" : 
                        "REST CYCLE COMPLETED. SYSTEM RESET. READY FOR NEXT TASK.", false);
                }
                return; 
            }
            
            uint32_t current_sec = (current_duration - elapsed) / 1000;
            if (current_sec != last_sec_draw) {
                last_sec_draw = current_sec;
                drawUI(); 
            }
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (is_paused) {
            is_paused = false;
            timer_start += (millis() - pause_start_time); 
        } else {
            is_paused = true;
            pause_start_time = millis();
        }
        drawUI();
    }

    void onKeyLong() override {
        SYS_SOUND_NAV();
        if (phase == 0) {
            phase = 1;
            current_duration = current_preset.rest_min * 60000;
            timer_start = millis();
            is_paused = false;
            last_sec_draw = 0xFFFFFFFF;

            PushNotify_Trigger_Custom(appManager.getLanguage() == LANG_ZH ? 
                "立刻去休息。" : 
                "Go rest immadiately.", true);
        } else {
            appManager.popApp();
        }
    }
};
AppPomodoroRun instancePomodoroRun; AppBase* appPomodoroRun = &instancePomodoroRun;

class AppPomodoroEdit : public AppBase {
private:
    int phase; 
    int t_work;
    int t_rest;
    PomodoroPreset* p_preset;

    // 【全新UI】：左标题，右参数，极简居中
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;
        int center_y = 30;

        // 左侧菜单标题
        const char* left_title = (phase == 0) ? (zh ? "专注时长" : "WORK TIME") : (zh ? "休息时长" : "REST TIME");
        HAL_Screen_ShowChineseLine(10, center_y, left_title);

        // 右侧参数值
        char val_buf[16];
        sprintf(val_buf, "%d", (phase == 0) ? t_work : t_rest);
        const char* suff = zh ? " 分钟 <" : " MIN <";
        
        int text_w = HAL_Get_Text_Width(val_buf) + HAL_Get_Text_Width(suff);
        drawSegmentedAnimatedText(sw - text_w - 10, center_y, "", val_buf, suff, 0.0f);
        
        // 底部操作指引
        const char* tip = zh ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: OK";
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, 60, tip, 0.6f);
        
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        p_preset = &sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx];
        t_work = p_preset->work_min;
        t_rest = p_preset->rest_min;
        phase = 0;
        drawUI();
    }
    void onResume() override { drawUI(); }
    void onLoop() override { if (updateEditAnimation()) drawUI(); }
    void onDestroy() override {}
    
    void onKnob(int delta) override {
        if (phase == 0) {
            t_work += delta;
            if (t_work < 1) t_work = 120; 
            if (t_work > 120) t_work = 1;
        } else {
            t_rest += delta;
            if (t_rest < 1) t_rest = 60;
            if (t_rest > 60) t_rest = 1;
        }
        triggerEditAnimation(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1;
            drawUI();
        } else {
            p_preset->work_min = t_work;
            p_preset->rest_min = t_rest;
            sysConfig.save();
            appManager.popApp();
        }
    }
    
    void onKeyLong() override {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};
AppPomodoroEdit instancePomodoroEdit; AppBase* appPomodoroEdit = &instancePomodoroEdit;

class AppPomodoroPresets : public AppMenuBase {
protected:
    int getMenuCount() override { return 5; }
    const char* getTitle() override { return (appManager.getLanguage() == LANG_ZH) ? "选择预设配置" : "SELECT PRESET"; }
    const char* getItemText(int index) override {
        static char buf[64];
        PomodoroPreset& p = sysConfig.pomodoro_presets[index];
        const char* mark = (index == sysConfig.pomodoro_current_idx) ? " <" : "";
        sprintf(buf, "%s (%d/%d)%s", p.name.c_str(), p.work_min, p.rest_min, mark);
        return buf;
    }
    void onItemClicked(int index) override {
        sysConfig.pomodoro_current_idx = index;
        sysConfig.save(); 
        appManager.popApp(); 
    }
    void onLongPressed() override { appManager.popApp(); }
};
AppPomodoroPresets instancePomodoroPresets; AppBase* appPomodoroPresets = &instancePomodoroPresets;

class AppPomodoroMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 3; } 
    const char* getTitle() override { return (appManager.getLanguage() == LANG_ZH) ? "番茄专注协议" : "POMODORO"; }
    const char* getItemText(int index) override {
        static char buf[64];
        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) { sprintf(buf, "执行专注 [%s]", sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx].name.c_str()); return buf; }
            if (index == 1) return "选择系统预设库";
            if (index == 2) return "修改当前预设时间"; 
        } else {
            if (index == 0) { sprintf(buf, "RUN [%s]", sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx].name.c_str()); return buf; }
            if (index == 1) return "SELECT PRESET";
            if (index == 2) return "EDIT CURRENT PRESET";
        }
        return "";
    }
    void onItemClicked(int index) override {
        if (index == 0) appManager.pushApp(appPomodoroRun);
        if (index == 1) appManager.pushApp(appPomodoroPresets);
        if (index == 2) appManager.pushApp(appPomodoroEdit);
    }
    void onLongPressed() override { appManager.popApp(); }
public:
    void onResume() override { AppMenuBase::onResume(); }
};
AppPomodoroMenu instancePomodoroMenu; AppBase* appPomodoro = &instancePomodoroMenu;