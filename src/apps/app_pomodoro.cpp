/*
【模块职责】番茄钟协议。管理 5 组预设、工作/休息运行页和编辑页，网页 POM 命令可更新预设。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_pomodoro.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "../ui/ui_frame.h"
#include "sys_config.h"
#include "sys_event.h"
#include "sys_command_result.h"
#include "sys_constants.h"

// 【函数说明】处理 POM 命令：校验槽位和时长后更新对应番茄钟预设名称、工作分钟和休息分钟，并保存配置。
void Pomodoro_UpdatePreset(int index, const char* name, int work_m, int rest_m) {
    if (index < 0 || index >= PrescriptConst::MAX_POMODORO_PRESETS)
    {
        SysCmdResult_Error("INVALID_SLOT");
        return;
    }
    if (work_m <= 0 || rest_m <= 0)
    {
        SysCmdResult_Error("INVALID_DURATION");
        return;
    }

    String safeName = String(name);
    safeName.trim();
    if (safeName.length() == 0)
        safeName = "PRESET";

    sysConfig.pomodoro_presets[index].name = safeName;
    sysConfig.pomodoro_presets[index].work_min = work_m;
    sysConfig.pomodoro_presets[index].rest_min = rest_m;
    sysConfig.save();
    SysCmdResult_Ok("UPDATED", String(index));
}

// 【函数说明】事件总线回调：把 Router 解析出的番茄预设参数交给 Pomodoro_UpdatePreset。
void _Cb_PomUpd(void* payload);

class AppPomodoroRun : public AppBase {
private:
    int phase; 
    uint32_t timer_start;
    uint32_t current_duration;
    uint32_t last_sec_draw;
    bool is_paused;
    uint32_t pause_start_time;
    PomodoroPreset current_preset;

    // 【函数说明】绘制番茄钟运行页：显示当前工作/休息阶段、剩余时间和暂停状态。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int center_y = 30;
        
        HAL_Screen_ShowChineseLine(10, center_y, current_preset.name.c_str());
        
        uint32_t elapsed = is_paused ? (pause_start_time - timer_start) : (millis() - timer_start);
        uint32_t remain = (current_duration > elapsed) ? (current_duration - elapsed) : 0;
        
        char time_buf[16];
        sprintf(time_buf, "%02d:%02d", remain / 60000, (remain / 1000) % 60);
        int tw = HAL_Get_Text_Width(time_buf);
        HAL_Screen_ShowTextLine(sw - tw - 10, center_y, time_buf);
        
        const char* state_str;
        if (appManager.getLanguage() == LANG_ZH) {
            if (is_paused) state_str = "已暂停 (单击继续 / 长按终止)";
            else state_str = (phase == 0) ? "正在执行专注..." : "正在休眠恢复...";
        } else {
            if (is_paused) state_str = "PAUSED (CLICK:RESUME/LONG:STOP)";
            else state_str = (phase == 0) ? "WORKING..." : "RESTING...";
        }
        HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(state_str))/2, 56, state_str, 0.4f);
        
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入运行页时读取当前预设，启动工作阶段计时并绘制第一帧。
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

    // 【函数说明】每秒更新剩余时间，工作到点后切休息，休息到点后提示完成并返回待启动状态。
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

    // 【函数说明】短按暂停或继续番茄钟计时。
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

    // 【函数说明】长按结束本次番茄钟并返回预设菜单。
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

    DialAnimator dialAnim;        // 实例化刻度盘引擎
    TacticalLinkEngine linkAnim;  // 实例化战术链路引擎

    // 【函数说明】绘制番茄预设编辑器：流程链路显示名称/工作/休息，滚轮调整工作和休息分钟。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        // 1. 顶部战术链路区
        const char* names_zh[] = {"专注时长", "休息时长"};
        const char* names_en[] = {"WORK MIN", "REST MIN"};
        const char** names = zh ? names_zh : names_en;
        
        linkAnim.draw(UITheme::EditFlow::LinkY(), names, 2, phase, 120);

        // 2. 中间机甲分隔线
        UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

        // 3. 底部动态机械刻度盘
        if (phase == 0) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_work, 1, 120, "");
        else dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), t_rest, 1, 60, "");

        // 4. 底部状态与操作指引
        const char* tip = zh ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: OK";
        UIFrame::DrawTip(tip);
        
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入预设编辑页时读取当前预设参数并重置编辑阶段。
    void onCreate() override {
        p_preset = &sysConfig.pomodoro_presets[sysConfig.pomodoro_current_idx];
        t_work = p_preset->work_min;
        t_rest = p_preset->rest_min;
        phase = 0;
        linkAnim.jumpTo(phase);
        drawUI();
    }
    void onResume() override { drawUI(); }
    
    void onLoop() override { 
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if(d_anim || l_anim) drawUI();
    }
    
    void onDestroy() override {}
    
    // 【函数说明】旋钮调整工作分钟或休息分钟，并触发数字滚轮动画。
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
        
        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    // 【函数说明】短按推进编辑阶段；最后一阶段写回 sysConfig.pomodoro_presets 并保存。
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
    
    // 【函数说明】长按放弃编辑并返回预设列表。
    void onKeyLong() override {
        SYS_SOUND_NAV();
        if (phase == 1) {
            phase = 0;
            drawUI();
        } else {
            appManager.popApp();
        }
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
    // 【函数说明】选择某个预设作为当前番茄钟配置，并进入运行页。
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
    // 【函数说明】番茄主菜单点击入口：开始当前预设、选择预设、编辑预设。
    void onItemClicked(int index) override {
        if (index == 0) appManager.push(AppId::PomodoroRun);
        if (index == 1) appManager.push(AppId::PomodoroPresets);
        if (index == 2) appManager.push(AppId::PomodoroEdit);
    }
    void onLongPressed() override { appManager.popApp(); }
public:
    void onResume() override { AppMenuBase::onResume(); }
    // 【函数说明】订阅 POMODORO_UPDATE 事件，让网页 POM 命令能更新预设。
    void onSystemInit() override{
    SysEvent_Subscribe(EVT_POMODORO_UPDATE, _Cb_PomUpd);
    appManager.registerBackgroundApp(this);
}
};
// === 番茄钟专属拆包回调 ===
// 【函数说明】事件总线回调：把 Router 解析出的番茄预设参数交给 Pomodoro_UpdatePreset。
void _Cb_PomUpd(void* payload) {
    Evt_PomUpd_t* p = (Evt_PomUpd_t*)payload;
    // 呼叫原本用于更新的接口
    // 【函数说明】处理 POM 命令：校验槽位和时长后更新对应番茄钟预设名称、工作分钟和休息分钟，并保存配置。
    extern void Pomodoro_UpdatePreset(int idx, const char* name, int w, int r);
    Pomodoro_UpdatePreset(p->idx, p->name, p->w, p->r);
}

// 供系统开机时调用的注册入口

AppPomodoroMenu instancePomodoroMenu; AppBase* appPomodoro = &instancePomodoroMenu;
