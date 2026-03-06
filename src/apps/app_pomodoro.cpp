// 文件：src/apps/app_pomodoro.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys_config.h"

// ----------------------------------------------------
// 【暴露给外部的手机接口】：直接写入硬盘
// ----------------------------------------------------
void Pomodoro_UpdatePreset(int index, const char* name, int work_m, int rest_m) {
    if (index < 0 || index >= 5) return;
    sysConfig.pomodoro_presets[index].name = name;
    sysConfig.pomodoro_presets[index].work_min = work_m;
    sysConfig.pomodoro_presets[index].rest_min = rest_m;
    sysConfig.save();
}

// 提前声明三个微应用的指针
extern AppBase* appPomodoroRun;
extern AppBase* appPomodoroPresets;
extern AppBase* appPomodoroEdit;

// ----------------------------------------------------
// 微模块 1：番茄钟核心执行引擎 (重写对齐与提前结束逻辑)
// ----------------------------------------------------
class AppPomodoroRun : public AppBase {
private:
    int phase; // 0:专注, 1:休息
    uint32_t timer_start;
    uint32_t current_duration;
    uint32_t last_sec_draw;
    bool is_paused;
    uint32_t pause_start_time;
    PomodoroPreset current_preset;

    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        
        // 顶部显示当前预设的专属名字
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, current_preset.name.c_str());
        
        char time_str[10];
        SysTime_GetTimeString(time_str);
        int time_x = sw - UI_TIME_SAFE_PAD - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, UI_TEXT_Y_TOP, time_str);
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

        int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2;

        uint32_t elapsed = is_paused ? (pause_start_time - timer_start) : (millis() - timer_start);
        uint32_t remain = (current_duration > elapsed) ? (current_duration - elapsed) : 0;
        
        char time_buf[16];
        sprintf(time_buf, "%02d:%02d", remain / 60000, (remain / 1000) % 60);
        
       int tw = HAL_Get_Text_Width(time_buf);
        int box_w = tw + 40;  
        int box_h = 32;       
        
        int box_x = (sw - box_w) / 2;
        int box_y = center_y - 16;  
        HAL_Draw_Rect(box_x, box_y, box_w, box_h, 1);
        
        // 【像素级微调】：数学上是 (sw - tw) / 2。
        // 因为字体右边距较宽，我们强行让文字整体向左移动 4 个像素（你可以根据强迫症程度改这个数字）
        int text_offset_x = 10; 
        int text_x = (sw - tw) / 2 - text_offset_x; 
        int text_y = center_y - 8;  
        HAL_Screen_ShowTextLine(text_x, text_y, time_buf);
        
        const char* state_str;
        if (appManager.getLanguage() == LANG_ZH) {
            if (is_paused) state_str = "已暂停 (单击继续 / 长按终止)";
            else state_str = (phase == 0) ? "正在执行专注..." : "正在休眠恢复...";
        } else {
            if (is_paused) state_str = "PAUSED (CLICK:RESUME/LONG:STOP)";
            else state_str = (phase == 0) ? "WORKING..." : "RESTING...";
        }
        HAL_Screen_ShowChineseLine((sw - HAL_Get_Text_Width(state_str))/2, center_y + 26, state_str);
        
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
                    
                    Prescript_Launch_Custom(appManager.getLanguage() == LANG_ZH ? 
                        "专注周期结束。立刻起身活动恢复精力。" : 
                        "WORK CYCLE COMPLETED. REST IMMEDIATELY.");
                    appManager.pushApp(appPrescript); 
                } else {
                    appManager.popApp(); 
                    Prescript_Launch_Custom(appManager.getLanguage() == LANG_ZH ? 
                        "休眠恢复完毕。系统已重置，准备接受新的专注指令。" : 
                        "REST CYCLE COMPLETED. SYSTEM RESET. READY FOR NEXT TASK.");
                    appManager.pushApp(appPrescript);
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
        // 【逻辑升级】：提前终止专注 -> 强行进入休息
        if (phase == 0) {
            phase = 1;
            current_duration = current_preset.rest_min * 60000;
            timer_start = millis();
            is_paused = false;
            last_sec_draw = 0xFFFFFFFF;

            Prescript_Launch_Custom(appManager.getLanguage() == LANG_ZH ? 
                "立刻去休息。" : 
                "Go rest immadiately.");
            appManager.pushApp(appPrescript);
        } else {
            // 如果已经在休息了，长按则彻底退出番茄钟
            appManager.popApp();
        }
    }
};

AppPomodoroRun instancePomodoroRun;
AppBase* appPomodoroRun = &instancePomodoroRun;


// ----------------------------------------------------
// 微模块 2：修改当前预设时间（使用跳动引擎）
// ----------------------------------------------------
class AppPomodoroEdit : public AppBase {
private:
    int phase; // 0: 设置专注, 1: 设置休息
    int t_work;
    int t_rest;
    PomodoroPreset* p_preset;

    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        
        char title_buf[64];
        if (appManager.getLanguage() == LANG_ZH) sprintf(title_buf, "参数重载 [%s]", p_preset->name.c_str());
        else sprintf(title_buf, "EDIT [%s]", p_preset->name.c_str());

        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title_buf);
        char time_str[10];
        SysTime_GetTimeString(time_str);
        int time_x = sw - UI_TIME_SAFE_PAD - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, UI_TEXT_Y_TOP, time_str);
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

        int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2;

        char val_buf[16];
        const char* pref;
        const char* suff = (appManager.getLanguage() == LANG_ZH) ? " 分钟 <" : " MIN <";
        
        if (phase == 0) {
            pref = (appManager.getLanguage() == LANG_ZH) ? "专注时长: " : "WORK: ";
            sprintf(val_buf, "%d", t_work);
        } else {
            pref = (appManager.getLanguage() == LANG_ZH) ? "休息时长: " : "REST: ";
            sprintf(val_buf, "%d", t_rest);
        }
        
        int text_w = HAL_Get_Text_Width(pref) + HAL_Get_Text_Width(val_buf) + HAL_Get_Text_Width(suff);
        int start_x = (sw - text_w) / 2;
        
        drawSegmentedAnimatedText(start_x, center_y - 8, pref, val_buf, suff, 0.0f);
        
        const char* tip = (appManager.getLanguage() == LANG_ZH) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: OK";
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        
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
            if (t_work < 1) t_work = 120; // 越界循环防呆
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
            // 写入预设并保存到硬盘
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

AppPomodoroEdit instancePomodoroEdit;
AppBase* appPomodoroEdit = &instancePomodoroEdit;


// ----------------------------------------------------
// 微模块 3：番茄钟 3D 预设列表菜单
// ----------------------------------------------------
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

AppPomodoroPresets instancePomodoroPresets;
AppBase* appPomodoroPresets = &instancePomodoroPresets;


// ----------------------------------------------------
// 微模块 4：番茄钟 3D 主菜单
// ----------------------------------------------------
class AppPomodoroMenu : public AppMenuBase {
protected:
    // 【完美修复】：选项增加到 3 个，3D 轮盘的数学算法将自动形成完美的无限循环！
    int getMenuCount() override { return 3; } 
    
    const char* getTitle() override { return (appManager.getLanguage() == LANG_ZH) ? "番茄专注协议" : "POMODORO"; }
    
    const char* getItemText(int index) override {
        static char buf[64];
        if (appManager.getLanguage() == LANG_ZH) {
            if (index == 0) {
                sprintf(buf, "执行专注 [%s]", sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx].name.c_str());
                return buf;
            }
            if (index == 1) return "选择系统预设库";
            if (index == 2) return "修改当前预设时间"; // 新增的编辑入口
        } else {
            if (index == 0) {
                sprintf(buf, "RUN [%s]", sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx].name.c_str());
                return buf;
            }
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
    void onResume() override {
        AppMenuBase::onResume(); 
    }
};

AppPomodoroMenu instancePomodoroMenu;
AppBase* appPomodoro = &instancePomodoroMenu;