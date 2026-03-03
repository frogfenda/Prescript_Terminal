#include "ui_render.h"
#include "hal.h" 

static SystemLang_t current_lang = LANG_ZH;

void UI_Set_Language(SystemLang_t lang) { current_lang = lang; }
SystemLang_t UI_Get_Language(void) { return current_lang; }
void UI_Toggle_Language(void) { current_lang = (current_lang == LANG_EN) ? LANG_ZH : LANG_EN; }

// 完全解耦：多语言字典现在只存在于 UI 层
static const char* UI_DICT_TITLE[2] = {
    "MAIN CONSOLE", 
    "系统主控制台"
};

static const char* UI_DICT_MENU[2][5] = {
    { "EXECUTE PRESCRIPT", "NETWORK TIME SYNC", "SWITCH LANGUAGE", "SLEEP SETTINGS", "RETURN STANDBY" }, 
    { "运行都市程序", "同步网络时间", "切换系统语言", "设定休眠时间", "返回待机画面" }                 
};

void UI_DrawMenu(int current_selection) {
    HAL_Screen_Clear();
    HAL_Screen_DrawHeader(); 
    
    // 渲染标题
    HAL_Screen_ShowChineseLine(70, 16, UI_DICT_TITLE[current_lang]);
    HAL_Draw_Line(20, 38, 220, 38, 1);

    int start_y = 52;
    int spacing = 30; 

    for (int i = 0; i < 5; i++) {
        if (i == current_selection) {
            HAL_Draw_Rect(10, start_y + i * spacing - 4, 220, 26, 1); 
            
            HAL_Screen_ShowChineseLine(40, start_y + i * spacing - 2, UI_DICT_MENU[current_lang][i]);
            
            HAL_Fill_Triangle(20, start_y + i * spacing,
                              20, start_y + i * spacing + 14,
                              27, start_y + i * spacing + 7, 1); 
                                    
            HAL_Fill_Rect(190, start_y + i * spacing + 2, 8, 14, 1);
        } else {
            HAL_Screen_ShowChineseLine(40, start_y + i * spacing - 2, UI_DICT_MENU[current_lang][i]);
        }
    }
    HAL_Screen_Update();
}

void UI_DrawNetworkSyncScreen(void) {
    HAL_Screen_Clear();
    HAL_Screen_DrawHeader();
    HAL_Screen_ShowChineseLine(40, 84, (current_lang == LANG_ZH) ? "网络未配置..." : "Network Offline..."); 
    HAL_Screen_Update();
}
// 在 src/ui_render.cpp 的末尾加入此函数：
void UI_DrawSleepSetting(int option_idx) {
    HAL_Screen_Clear();
    HAL_Screen_DrawHeader();

    // 绘制标题
    HAL_Screen_ShowChineseLine(70, 16, UI_Get_Language() == LANG_ZH ? "休眠时间设定" : "SLEEP SETTINGS");
    HAL_Draw_Line(20, 38, 220, 38, 1);

    int start_y = 90;
    
    // 绘制选中框
    HAL_Draw_Rect(20, start_y - 4, 200, 26, 1);
    HAL_Fill_Triangle(30, start_y, 30, start_y + 14, 37, start_y + 7, 1);

    // 4个时间选项字典
    const char* opts_zh[] = {"30 秒 (推荐)", "60 秒", "5 分钟", "永不休眠"};
    const char* opts_en[] = {"30 SECONDS", "60 SECONDS", "5 MINUTES", "NEVER SLEEP"};
    const char* text = (UI_Get_Language() == LANG_ZH) ? opts_zh[option_idx] : opts_en[option_idx];

    // 渲染选项文字
    HAL_Screen_ShowChineseLine(50, start_y - 2, text);

    // 底部操作提示
    HAL_Screen_ShowChineseLine(40, 160, UI_Get_Language() == LANG_ZH ? "按下旋钮保存并返回" : "PRESS TO SAVE & RETURN");
    
    HAL_Screen_Update();
}