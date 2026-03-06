#include "app_base.h"
#include "app_manager.h"

class AppPushNotify : public AppBase {
private:
    uint32_t blink_timer;
    bool show_text;

public:
    void onCreate() override {
        blink_timer = millis();
        show_text = true;
        
        // 突发事件警报音效！
        HAL_Buzzer_Play_Tone(1500, 200); delay(100);
        HAL_Buzzer_Play_Tone(1500, 200); delay(100);
        HAL_Buzzer_Play_Tone(2500, 600);
    }

    void onLoop() override {
        // 字体闪烁逻辑
        if (millis() - blink_timer > 600) {
            blink_timer = millis();
            show_text = !show_text;
            
            // 彻底黑屏，不要画任何顶部状态栏！
            HAL_Screen_Clear();
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
        // 关键：挂上标志位，直接把页面切进指令 APP！
        g_prescript_direct_decode = true; 
        appManager.launchApp(appPrescript);
    }
    void onKeyLong() override { onKeyShort(); }
};

AppPushNotify instancePushNotify;
AppBase* appPushNotify = &instancePushNotify;