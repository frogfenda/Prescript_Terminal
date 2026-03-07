// 文件：src/apps/app_push_notify.cpp
#include "app_base.h"
#include "app_manager.h"
#include <stdio.h>
#include <string.h>

int g_push_notify_mode = 0; // 0: random, 1: custom
char g_push_notify_text[512] = {0};
bool g_push_notify_keep_stack = false;

// 【接口 1：随机指令警报】
void PushNotify_Trigger_Random(bool keep_stack) {
    g_push_notify_mode = 0;
    g_push_notify_keep_stack = keep_stack;
    if (keep_stack) appManager.pushApp(appPushNotify);
    else appManager.launchApp(appPushNotify);
}

// 【接口 2：自定义固定指令警报】
void PushNotify_Trigger_Custom(const char* text, bool keep_stack) {
    g_push_notify_mode = 1;
    snprintf(g_push_notify_text, sizeof(g_push_notify_text), "%s", text);
    g_push_notify_keep_stack = keep_stack;
    if (keep_stack) appManager.pushApp(appPushNotify);
    else appManager.launchApp(appPushNotify);
}

class AppPushNotify : public AppBase {
private:
    uint32_t blink_timer;
    bool show_text;

public:
    void onCreate() override {
        blink_timer = millis();
        show_text = true;
        
        // 初始突发事件警报音效！
        HAL_Buzzer_Play_Tone(1500, 200); delay(100);
        HAL_Buzzer_Play_Tone(1500, 200); delay(100);
        HAL_Buzzer_Play_Tone(2500, 600);
    }

    void onLoop() override {
        if (millis() - blink_timer > 600) {
            blink_timer = millis();
            show_text = !show_text;
            
            // 【特性新增】：每闪烁一次就发出极其短促尖锐的滴声！
            if (show_text) {
                HAL_Buzzer_Play_Tone(2500, 50); 
            }
            
            // 使用 Sprite 消除全屏撕裂
            HAL_Sprite_Clear();
            if (show_text) {
                const char* txt = (appManager.getLanguage() == LANG_ZH) ? "接受都市意志" : "RECEIVE PRESCRIPT";
                int x = (HAL_Get_Screen_Width() - HAL_Get_Text_Width(txt)) / 2;
                HAL_Screen_ShowChineseLine(x, HAL_Get_Screen_Height()/2 - 8, txt);
            }
            HAL_Screen_Update();
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        
        // 【智能分流】：根据传入的接口决定解密文字
        if (g_push_notify_mode == 0) Prescript_Launch_PushDirect(); 
        else Prescript_Launch_Custom(g_push_notify_text);

        // 【智能堆栈】：看完解密动画后，是否要退回之前的 App？
        if (g_push_notify_keep_stack) {
            appManager.popApp(); // 拔除自己
            appManager.pushApp(appPrescript); // 压入解密界面，解密完就能退回番茄钟
        } else {
            appManager.launchApp(appPrescript); // 霸占屏幕，解密完退回主菜单
        }
    }
    void onKeyLong() override { onKeyShort(); }
};

AppPushNotify instancePushNotify;
AppBase* appPushNotify = &instancePushNotify;