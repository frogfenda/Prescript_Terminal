#include "ui_render.h"
#include "hal.h" 

static SystemLang_t current_lang = LANG_ZH;

void UI_Set_Language(SystemLang_t lang) { current_lang = lang; }
SystemLang_t UI_Get_Language(void) { return current_lang; }
void UI_Toggle_Language(void) { current_lang = (current_lang == LANG_EN) ? LANG_ZH : LANG_EN; }

static const char* UI_DICT_TITLE[2] = { "MAIN CONSOLE", "系统主控制台" };
static const char* UI_DICT_MENU[2][5] = {
    { "EXECUTE PRESCRIPT", "NETWORK TIME SYNC", "SWITCH LANGUAGE", "SLEEP SETTINGS", "RETURN STANDBY" }, 
    { "运行都市程序", "同步网络时间", "切换系统语言", "设定休眠时间", "返回待机画面" }                 
};



// 修复链接错误的函数实现
void UI_DrawSleepSetting(int option_idx) {
    HAL_Screen_Clear();
    HAL_Screen_DrawHeader();
    HAL_Screen_ShowChineseLine(70, 16, current_lang == LANG_ZH ? "休眠时间设定" : "SLEEP SETTINGS");
    HAL_Draw_Line(20, 38, 220, 38, 1);

    int start_y = 90;
    HAL_Draw_Rect(20, start_y - 4, 200, 26, 1);
    HAL_Fill_Triangle(30, start_y, 30, start_y + 14, 37, start_y + 7, 1);

    const char* opts_zh[] = {"30 秒 (推荐)", "60 秒", "5 分钟", "永不休眠"};
    const char* opts_en[] = {"30 SECONDS", "60 SECONDS", "5 MINUTES", "NEVER SLEEP"};
    HAL_Screen_ShowChineseLine(50, start_y - 2, (current_lang == LANG_ZH) ? opts_zh[option_idx] : opts_en[option_idx]);
    HAL_Screen_Update();
}

void UI_DrawNetworkSyncScreen(void) {
    HAL_Screen_Clear();
    HAL_Screen_DrawHeader();
    HAL_Screen_ShowChineseLine(40, 84, (current_lang == LANG_ZH) ? "网络未配置..." : "Network Offline..."); 
    HAL_Screen_Update();
}
void UI_DrawMenu_Animated(float visual_pos) {
    // 【核心修改】：不再调用 HAL_Screen_Clear()
    HAL_Sprite_Clear(); 
    
    // 注意：Header 现在由 FSM 在进入菜单时统一画一次，或者在此处重画（但只画在 Sprite 里）
    // 为了保证解耦，我们将菜单标题和线条也视为 Sprite 内容
    HAL_Screen_ShowChineseLine(70, 16, UI_DICT_TITLE[UI_Get_Language()]);
    HAL_Draw_Line(20, 38, 220, 38, 1);

    const int start_y = 52;
    const int spacing = 30; 

    for (int i = 0; i < 5; i++) {
        HAL_Screen_ShowChineseLine(40, start_y + i * spacing - 2, UI_DICT_MENU[UI_Get_Language()][i]);
    }

    float draw_y = start_y + visual_pos * spacing;
    HAL_Draw_Rect(10, (int)draw_y - 4, 220, 26, 1); 
    HAL_Fill_Triangle(20, (int)draw_y, 20, (int)draw_y + 14, 27, (int)draw_y + 7, 1); 
    HAL_Fill_Rect(190, (int)draw_y + 2, 8, 14, 1);

    HAL_Screen_Update(); // 只有这一步会将 Sprite 推送到物理屏幕
}