// 文件：src/apps/app_countdown.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys/sys_audio.h"
#include "hal/hal.h"

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
void Countdown_Start(int min, int sec, const char* custom_cmd = nullptr) {
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
            // 【危险操作界面】
            const char *title = zh ? "危险操作" : "DANGER";
            HAL_Screen_ShowChineseLine(10, 26, title);

            const char *buf = zh ? "确认撤收任务 ?" : "ABORT PROTOCOL ?";
            int txt_w = HAL_Get_Text_Width(buf);
            HAL_Screen_ShowChineseLine(sw - txt_w - 10, 26, buf);

            const char *tip = zh ? "长按取消 / 单击撤收" : "LONG: CANCEL / CLICK: ABORT";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        else {
            // 【机甲引擎主界面】
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
            if (phase == 0 || phase == 1) {
                // 【沉浸式文案】：TT2 协议专属设定语
                tip = zh ? "你想到达多久之后的未来" : "HOW FAR INTO THE FUTURE?";
            } else if (phase == 2) {
                tip = zh ? "长按退出(后台运行) / 单击撤收" : "LONG: EXIT(BG) / CLICK: ABORT";
            } else {
                tip = zh ? "长按取消 / 单击撤收" : "LONG: CANCEL / CLICK: ABORT";
            }

            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        HAL_Screen_Update();
    }

public:
    void onSystemInit() override {
        // 向系统管家申请后台巡逻权限，这是实现“跨应用弹窗”的核心！
        appManager.registerBackgroundApp(this);
    }

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
                if (appManager.getLanguage() == LANG_ZH) {
                    sprintf(t_buf, "TT2协议结束,你已经到达%d分%d秒后的未来", g_countdown_set_min, g_countdown_set_sec);
                } else {
                    sprintf(t_buf, "TT2 PROTOCOL ENDED. REACHED %dM %dS IN FUTURE.", g_countdown_set_min, g_countdown_set_sec);
                }
                notify_cmd = t_buf;
            }
            
            // 去掉 TXT，直接推纯文本
            PushNotify_Trigger_Custom(notify_cmd.c_str(), false);
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