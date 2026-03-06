// 文件：src/apps/app_pomodoro.cpp
#include "app_base.h"
#include "app_manager.h"

class AppPomodoro : public AppBase {
private:
    int phase; // 0:设专注时长, 1:设休息时长, 2:专注倒计时, 3:休息倒计时
    int work_min;
    int rest_min;
    uint32_t timer_start;
    uint32_t current_duration;
    uint32_t last_sec_draw;
    bool is_paused;
    uint32_t pause_start_time;

    void drawUI() {
        HAL_Screen_Clear();
        drawAppWindow(appManager.getLanguage() == LANG_ZH ? "番茄专注协议" : "POMODORO");

        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2;

        if (phase == 0 || phase == 1) {
            // ==========================================
            // 【UI 接口 1】：设定阶段，复用分段跳动动画引擎
            // ==========================================
            char val_buf[16];
            const char* pref;
            const char* suff = (appManager.getLanguage() == LANG_ZH) ? " 分钟 <" : " MIN <";
            
            if (phase == 0) {
                pref = (appManager.getLanguage() == LANG_ZH) ? "专注时长: " : "WORK: ";
                sprintf(val_buf, "%d", work_min);
            } else {
                pref = (appManager.getLanguage() == LANG_ZH) ? "休息时长: " : "REST: ";
                sprintf(val_buf, "%d", rest_min);
            }
            
            int text_w = HAL_Get_Text_Width(pref) + HAL_Get_Text_Width(val_buf) + HAL_Get_Text_Width(suff);
            int start_x = (sw - text_w) / 2;
            
            // 呼叫带残影的渲染器
            drawSegmentedAnimatedText(start_x, center_y - 8, pref, val_buf, suff, 0.0f);
            
            const char* tip = (appManager.getLanguage() == LANG_ZH) ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip))/2, sh - 20, tip, 0.6f);
        } else {
            // ==========================================
            // 倒计时运行阶段 UI
            // ==========================================
            uint32_t elapsed = is_paused ? (pause_start_time - timer_start) : (millis() - timer_start);
            uint32_t remain = (current_duration > elapsed) ? (current_duration - elapsed) : 0;
            
            char time_buf[16];
            sprintf(time_buf, "%02d:%02d", remain / 60000, (remain / 1000) % 60);
            
            int tw = HAL_Get_Text_Width(time_buf);
            HAL_Draw_Rect((sw - tw - 24)/2, center_y - 18, tw + 24, 32, 1);
            HAL_Screen_ShowTextLine((sw - tw)/2, center_y - 10, time_buf);
            
            const char* state_str;
            if (appManager.getLanguage() == LANG_ZH) {
                if (is_paused) state_str = "已暂停 (单击继续 / 长按终止)";
                else state_str = (phase == 2) ? "正在执行专注..." : "正在休眠恢复...";
            } else {
                if (is_paused) state_str = "PAUSED (CLICK: RESUME / LONG: STOP)";
                else state_str = (phase == 2) ? "WORKING..." : "RESTING...";
            }
            HAL_Screen_ShowChineseLine((sw - HAL_Get_Text_Width(state_str))/2, center_y + 24, state_str);
        }
        
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        phase = 0;
        work_min = 25; // 经典番茄钟法则：25分专注 + 5分休息
        rest_min = 5;
        is_paused = false;
        drawUI();
    }
    
    // 【核心联动】：当系统从强提醒指令弹窗退回来时，界面需要被重新点亮
    void onResume() override { drawUI(); }

    void onLoop() override {
        bool needs_draw = updateEditAnimation(); // 维持 UI1 的跳动检测
        
        if ((phase == 2 || phase == 3) && !is_paused) {
            uint32_t elapsed = millis() - timer_start;
            
            // 【倒计时归零时刻！】
            if (elapsed >= current_duration) {
                if (phase == 2) {
                    // 专注结束 -> 切换为休息
                    phase = 3;
                    current_duration = rest_min * 60000;
                    timer_start = millis();
                    
                    // ==========================================
                    // 【UI 接口 2】：利用自定义突发指令，强行接管屏幕提示！
                    // ==========================================
                    Prescript_Launch_Custom(appManager.getLanguage() == LANG_ZH ? 
                        "专注周期结束。强制切断当前任务，立刻起身活动恢复精力。" : 
                        "WORK CYCLE COMPLETED. FORCED DISCONNECT. REST IMMEDIATELY.");
                    appManager.pushApp(appPrescript); 
                    
                } else {
                    // 休息结束 -> 切换回设置状态
                    phase = 0;
                    Prescript_Launch_Custom(appManager.getLanguage() == LANG_ZH ? 
                        "休眠恢复完毕。系统已重置，准备接受新的专注指令。" : 
                        "REST CYCLE COMPLETED. SYSTEM RESET. READY FOR NEXT TASK.");
                    appManager.pushApp(appPrescript);
                }
                return; // 因为弹出了新页面，跳过当前帧渲染
            }
            
            // 倒计时每过一秒才重绘一次屏幕，极致节省算力防闪烁
            uint32_t current_sec = (current_duration - elapsed) / 1000;
            if (current_sec != last_sec_draw) {
                last_sec_draw = current_sec;
                needs_draw = true;
            }
        }
        
        if (needs_draw) drawUI();
    }

    void onDestroy() override {}

    void onKnob(int delta) override {
        if (phase == 0) {
            work_min += delta;
            if (work_min < 1) work_min = 1;
            if (work_min > 120) work_min = 120;
            triggerEditAnimation(delta); // 呼叫 UI1 跳动动画
            SYS_SOUND_GLITCH();
            drawUI();
        } else if (phase == 1) {
            rest_min += delta;
            if (rest_min < 1) rest_min = 1;
            if (rest_min > 60) rest_min = 60;
            triggerEditAnimation(delta); // 呼叫 UI1 跳动动画
            SYS_SOUND_GLITCH();
            drawUI();
        }
    }

    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1; drawUI();
        } else if (phase == 1) {
            phase = 2; // 开始执行专注！
            is_paused = false;
            current_duration = work_min * 60000; // 换算为毫秒
            timer_start = millis();
            last_sec_draw = 0xFFFFFFFF; 
            drawUI();
        } else if (phase == 2 || phase == 3) {
            // 运行状态下的单击：暂停/继续
            if (is_paused) {
                is_paused = false;
                timer_start += (millis() - pause_start_time); // 补偿暂停流失的时间
            } else {
                is_paused = true;
                pause_start_time = millis();
            }
            drawUI();
        }
    }

    void onKeyLong() override {
        SYS_SOUND_NAV();
        if (phase == 0) {
            appManager.popApp(); // 退出番茄钟
        } else if (phase == 1) {
            phase = 0; drawUI(); // 退回设置专注时间
        } else {
            phase = 0; drawUI(); // 中止运行，退回初始状态
        }
    }
};

AppPomodoro instancePomodoro;
AppBase* appPomodoro = &instancePomodoro;