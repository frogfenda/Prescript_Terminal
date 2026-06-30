/*
【模块职责】TT2 倒计时协议。提供全局倒计时状态、设置页和后台 tick，到点后弹出自定义指令或者默认完成指令。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_countdown.cpp
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"
#include "ui/ui_frame.h"
#include "apps/app_countdown.h"
#include "lang/ui_strings.h"

// ==========================================
// 暴露给 HUD 的全局变量与通信信道
// ==========================================
volatile bool g_countdown_active = false;
uint32_t g_countdown_end_time = 0;
String g_countdown_cmd = ""; 

// 【新增】：记录用户设定的时间锚点（用于最终播报）
int g_countdown_set_min = 0;
int g_countdown_set_sec = 0;

// ==========================================
// 【核心接口】：供外界其他应用“一键拉起”TT2协议！
// 调用方法：Countdown_Start(5, 0, "面条已经泡好了");
// 如果 custom_cmd 传 nullptr 或 ""，则默认走 TT2 协议文案
// ==========================================
// 【函数说明】启动全局 TT2 倒计时：计算结束时间，保存完成指令文本，并让 HUD 能显示 TMR 剩余时间。
void Countdown_Start(int min, int sec, const char* custom_cmd) {
    g_countdown_set_min = min;
    g_countdown_set_sec = sec;
    g_countdown_end_time = millis() + (min * 60 + sec) * 1000;
    
    if (custom_cmd && strlen(custom_cmd) > 0) {
        g_countdown_cmd = custom_cmd;
    } else {
        g_countdown_cmd = "";
    }
    g_countdown_active = true;
}

// 【函数说明】判断当前 millis 是否还没有超过结束时间，超过后自动认为倒计时结束。
bool Countdown_IsActive() {
    return g_countdown_active;
}

// 【函数说明】把结束时间减去当前 millis 转换成秒数，低于 0 时返回 0。
int Countdown_GetRemainingSeconds() {
    if (!g_countdown_active) return 0;
    uint32_t now = millis();
    if (g_countdown_end_time <= now) return 0;
    return (int)((g_countdown_end_time - now) / 1000);
}

class AppCountdown : public AppBase {
private:
    int m, s, phase;
    
    DialAnimator dialAnim;       // 机械滚轮引擎
    TacticalLinkEngine linkAnim; // 战术链路引擎

    int last_sec = -1;

    // 【函数说明】绘制 TT2 页面：设置阶段显示分钟/秒钟滚轮，运行阶段显示剩余 mm:ss，确认阶段显示中止提示。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        SystemLang_t lang = appManager.getLanguage();

        if (phase == 3) {
            // 【危险操作界面】
            UIFrame::DrawDangerConfirm(UIStrings::DangerTitle(lang),
                                       UIStrings::CountdownAbortTitle(lang),
                                       UIStrings::CountdownAbortHint(lang));
        }
        else {
            // 【机甲引擎主界面】
            // 1. 顶部战术链路
            linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::CountdownStepNames(lang), 3, phase, 85);

            // 2. 中间机甲分隔线 (Y=18)
            UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

            // 3. 底部动态内容区 (Y=28/32)
            if (phase == 0) {
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), m, 0, 99, "");
            } else if (phase == 1) {
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), s, 0, 59, "");
            } else if (phase == 2) {
                int remain = 0;
                if (g_countdown_active && g_countdown_end_time > millis()) {
                    remain = (g_countdown_end_time - millis()) / 1000;
                }
                char t_buf[16];
                sprintf(t_buf, "%02d : %02d", remain / 60, remain % 60);

                // 运行态倒计时显示放在战术分隔线下方的可用区域中间，
                // 避免原先固定 y=32 时字形下沿压到横线。
                int text_w = HAL_Get_Text_Width(t_buf);
                int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
                int area_top = UITheme::EditFlow::DividerY() + UITheme::Frame::DividerBevelH() + 10;
                int area_bottom = UITheme::EditFlow::TipY() - 8;
                int time_y = area_top;
                if (area_bottom > area_top + line_h) {
                    time_y = area_top + (area_bottom - area_top - line_h) / 2;
                }
                HAL_Screen_ShowTextLine((sw - text_w) / 2, time_y, t_buf);
            }

            // 4. 底部状态与操作指引 (Y=56)
            const char *tip;
            if (phase == 0 || phase == 1) {
                // 【沉浸式文案】：TT2 协议专属设定语
                tip = UIStrings::CountdownSetTip(lang);
            } else if (phase == 2) {
                tip = UIStrings::CountdownRunningTip(lang);
            } else {
                tip = UIStrings::CountdownAbortHint(lang);
            }

            UIFrame::DrawTip(tip);
        }
        HAL_Screen_Update();
    }

public:
    // 【函数说明】注册倒计时 App 为后台 App，让它在离开页面后仍能检查到点。
    void onSystemInit() override {
        // 向系统管家申请后台巡逻权限，这是实现“跨应用弹窗”的核心！
        appManager.registerBackgroundApp(this);
    }

    // 【函数说明】进入倒计时页面：读取上次设置的分钟/秒数，初始化阶段为设置分钟并绘制界面。
    void onCreate() override {
        if (g_countdown_active) {
            if (millis() > g_countdown_end_time) {
                g_countdown_active = false; 
                phase = 0; m = 5; s = 0;
            } else {
                phase = 2; // 后台正在跑，直接切到倒计时画面
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

    // 【函数说明】从其他页面返回时重绘 TT2 页面，保持剩余时间显示。
    void onResume() override { drawUI(); }

    void onLoop() override {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        bool time_tick = false;

        // 【极客联动】：如果在设定时被外部接口强行拉起倒计时，UI 瞬间跃迁到执行模式
        if (g_countdown_active && phase < 2) {
            phase = 2;
            linkAnim.jumpTo(2);
            drawUI();
        }

        if (phase == 2) {
            // 若被后台弹窗抢先处理完毕，或被外部取消，自动退栈
            if (!g_countdown_active) {
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

        if (d_anim || l_anim || time_tick) {
            drawUI();
        }
    }

    // 【函数说明】后台检查倒计时是否到点，到点后触发完成反馈并弹出 PushNotify。
    void onBackgroundTick() override {
        // ==========================================
        // 【核心守护】：到达未来！
        // ==========================================
        if (g_countdown_active && millis() >= g_countdown_end_time) {
            g_countdown_active = false;
            
            extern void PushNotify_Trigger_Custom(const char *custom_text, bool keep_stack);
            
            String notify_cmd = g_countdown_cmd;
            
            // 如果外界没有传入特定的指令，则生成 TT2 协议专属报幕！
            if (notify_cmd.isEmpty()) {
                char t_buf[128];
                snprintf(t_buf, sizeof(t_buf), UIStrings::CountdownDoneFormat(appManager.getLanguage()), g_countdown_set_min, g_countdown_set_sec);
                notify_cmd = t_buf;
            }
            
            // 去掉 TXT，直接推纯文本
            PushNotify_Trigger_Custom(notify_cmd.c_str(), false);
            Feedback_PlayTimerDone();
        }
    }

    void onDestroy() override {}

    // 【函数说明】设置阶段旋钮调整分钟或秒钟，分钟按 1 递增，秒钟在 0-59 内循环。
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

    // 【函数说明】短按推进“分钟→秒钟→启动”流程；运行阶段短按暂停/继续当前倒计时显示。
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase == 0) {
            phase = 1;
            drawUI();
        } 
        else if (phase == 1) {
            if (m == 0 && s == 0) return; // 0秒防呆
            phase = 2;
            g_countdown_cmd = ""; // 手工发起的 TT2 协议，清空外部传入指令
            g_countdown_set_min = m;
            g_countdown_set_sec = s;
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

    // 【函数说明】长按在运行阶段进入中止确认，再次长按退出页面。
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
            appManager.popApp(); 
        }
    }
};

AppCountdown instanceCountdown;
AppBase *appCountdown = &instanceCountdown; 
