// 文件：src/apps/app_countdown.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"

// 暴露给 HUD 的全局变量
volatile bool g_countdown_active = false;
uint32_t g_countdown_end_time = 0;

class AppCountdown : public AppBase {
private:
    int m, s, phase;
    
    DialAnimator dialAnim;       // 机械滚轮引擎
    TacticalLinkEngine linkAnim; // 战术链路引擎

    int last_sec = -1;

    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 3) {
            // ==========================================
            // 【危险操作界面】：完美复刻闹钟的删除确认 UI
            // ==========================================
            const char *title = zh ? "危险操作" : "DANGER";
            HAL_Screen_ShowChineseLine(10, 26, title);

            const char *buf = zh ? "确认撤收任务 ?" : "ABORT TIMER ?";
            int txt_w = HAL_Get_Text_Width(buf);
            HAL_Screen_ShowChineseLine(sw - txt_w - 10, 26, buf);

            const char *tip = zh ? "长按取消 / 单击撤收" : "LONG: CANCEL / CLICK: ABORT";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        else {
            // ==========================================
            // 【机甲引擎主界面】：三层复合结构
            // ==========================================
            const char *names_zh[] = {"设定分钟", "设定秒数", "执行中"};
            const char *names_en[] = {"SET MIN", "SET SEC", "RUNNING"};
            const char **names = zh ? names_zh : names_en;

            // 1. 顶部战术链路
            linkAnim.draw(2, names, 3, phase, 85);

            // 2. 中间机甲分隔线 (Y=18)
            int line_y = 18;
            HAL_Draw_Line(0, line_y, sw / 2 - 30, line_y, 1);
            HAL_Draw_Line(sw / 2 - 30, line_y, sw / 2 - 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 - 25, line_y + 3, sw / 2 + 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 + 25, line_y + 3, sw / 2 + 30, line_y, 1);
            HAL_Draw_Line(sw / 2 + 30, line_y, sw, line_y, 1);

            // 3. 底部动态内容区 (Y=28/32)
            if (phase == 0) {
                dialAnim.drawNumberDial(28, m, 0, 99, "");
            } else if (phase == 1) {
                dialAnim.drawNumberDial(28, s, 0, 59, "");
            } else if (phase == 2) {
                // 执行中，显示巨大数字
                int remain = 0;
                if (g_countdown_active && g_countdown_end_time > millis()) {
                    remain = (g_countdown_end_time - millis()) / 1000;
                }
                char t_buf[16];
                sprintf(t_buf, "%02d : %02d", remain / 60, remain % 60);
                HAL_Screen_ShowTextLine((sw - HAL_Get_Text_Width(t_buf)) / 2, 32, t_buf);
            }

            // 4. 底部状态与操作指引 (Y=56)
            const char *tip;
            if (phase == 0) tip = zh ? "长按退出 / 单击下一步" : "LONG: EXIT / CLICK: NEXT";
            else if (phase == 1) tip = zh ? "长按返回 / 单击开始" : "LONG: BACK / CLICK: START";
            else tip = zh ? "长按退出(后台运行) / 单击撤收" : "LONG: EXIT(BG) / CLICK: ABORT";

            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        if (g_countdown_active) {
            if (millis() > g_countdown_end_time) {
                g_countdown_active = false; // 已经过期了，重置
                phase = 0; m = 5; s = 0;
            } else {
                phase = 2; // 后台正在运行，直接跳到执行中
            }
        } else {
            phase = 0; 
            m = 5; 
            s = 0;
        }
        linkAnim.jumpTo(phase);
        last_sec = -1;
        drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        bool time_tick = false;

        // 如果在倒计时中，检测时间是否发生跳动
        if (phase == 2 && g_countdown_active) {
            if (millis() >= g_countdown_end_time) {
                // 【核心联动】：如果停留在界面时倒计时结束了，直接借助后台弹窗引擎拉起通知，并退出当前App！
                g_countdown_active = false;
                extern void PushNotify_Trigger_Custom(const char *custom_text, bool keep_stack);
                PushNotify_Trigger_Custom((appManager.getLanguage() == LANG_ZH) ? "倒计时结束!" : "TIMER UP!", false);
                sysAudio.playTone(3000, 300);
                appManager.popApp();
                return;
            } else {
                int current_sec = (g_countdown_end_time - millis()) / 1000;
                if (current_sec != last_sec) {
                    last_sec = current_sec;
                    time_tick = true;
                }
            }
        }

        // 只要有任何动画在动，或者秒数跳动了，就推屏
        if (d_anim || l_anim || time_tick) {
            drawUI();
        }
    }

    void onBackgroundTick() override {
        // 后台守护：时间到了全局拉起弹窗
        if (g_countdown_active && millis() >= g_countdown_end_time) {
            g_countdown_active = false;
            extern void PushNotify_Trigger_Custom(const char *custom_text, bool keep_stack);
            PushNotify_Trigger_Custom((appManager.getLanguage() == LANG_ZH) ? "倒计时结束!" : "TIMER UP!", true);
            sysAudio.playTone(3000, 300);
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override {
        if (phase == 0) {
            m += delta;
            if (m < 0) m = 99;
            if (m > 99) m = 0;
            dialAnim.trigger(delta);
            SYS_SOUND_GLITCH();
        } 
        else if (phase == 1) {
            s += delta;
            if (s < 0) s = 59;
            if (s > 59) s = 0;
            dialAnim.trigger(delta);
            SYS_SOUND_GLITCH();
        }
        drawUI();
    }

    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1;
            drawUI();
        } 
        else if (phase == 1) {
            if (m == 0 && s == 0) return; // 0秒防呆
            phase = 2;
            g_countdown_active = true;
            g_countdown_end_time = millis() + (m * 60 + s) * 1000;
            drawUI();
        } 
        else if (phase == 2) {
            phase = 3; // 弹出“危险操作”确认撤收
            drawUI();
        } 
        else if (phase == 3) {
            g_countdown_active = false;
            phase = 0; // 确认撤收，回到重设状态
            linkAnim.jumpTo(0);
            drawUI();
        }
    }

    void onKeyLong() override {
        if (phase == 3) {
            SYS_SOUND_NAV();
            phase = 2; // 取消撤收，返回倒计时
            drawUI();
        } 
        else if (phase == 1) {
            SYS_SOUND_NAV();
            phase = 0; // 取消设秒，返回设分
            drawUI();
        } 
        else {
            SYS_SOUND_NAV();
            appManager.popApp(); // phase=0或2时，直接退出（后台继续跑）
        }
    }
};

AppCountdown instanceCountdown;
AppBase *appCountdown = &instanceCountdown;