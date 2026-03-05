// 文件：src/ui_render.h
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

// 【全局通用组件】
void UI_DrawMenu_Animated(float visual_pos); 

#endif