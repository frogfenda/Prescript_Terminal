#ifndef __UI_RENDER_H
#define __UI_RENDER_H

#include <Arduino.h>

typedef enum
{
    LANG_EN = 0,
    LANG_ZH = 1
} SystemLang_t;

void UI_Set_Language(SystemLang_t lang);
SystemLang_t UI_Get_Language(void);
void UI_Toggle_Language(void);

// 【解耦接口】
void UI_DrawMenu_Animated(float visual_pos); // 主菜单：支持平滑动画
void UI_DrawSleepSetting(int option_idx);    // 设置页：简单的静态刷新
void UI_DrawNetworkSyncScreen(void);

#endif