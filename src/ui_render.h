#ifndef __UI_RENDER_H
#define __UI_RENDER_H

#include <Arduino.h>

typedef enum {
    LANG_EN = 0,
    LANG_ZH = 1
} SystemLang_t;

void UI_Set_Language(SystemLang_t lang);
SystemLang_t UI_Get_Language(void);
void UI_Toggle_Language(void);

void UI_DrawMenu(int current_selection);
void UI_DrawNetworkSyncScreen(void);
void UI_DrawSleepSetting(int option_idx); // 【新增】绘制休眠设置界面
#endif