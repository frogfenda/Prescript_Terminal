// 文件：src/ui_render.cpp
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

void UI_DrawMenu_Animated(float visual_pos) {
    HAL_Sprite_Clear(); 
    
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

    HAL_Screen_Update(); 
}