// 文件：src/apps/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "app_base.h"
#include "app_manager.h"
#include <time.h> 

class AppMenuBase : public AppBase {
protected:
    int current_selection;
    float visual_selection;

    virtual int getMenuCount() = 0;                  
    virtual const char* getTitle() = 0;              
    virtual const char* getItemText(int index) = 0;  
    virtual void onItemClicked(int index) = 0;       
    virtual void onLongPressed() = 0;                

    // 【新增】：虚函数，询问子类是否需要分段渲染。默认不需要。
    virtual bool getItemEditParts(int index, const char** prefix, const char** anim_val, const char** suffix) {
        return false;
    }

   void drawMenuUI(float v_pos) {
        HAL_Sprite_Clear(); 
        
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        int center_x = sw / 2; 

        const char* title_text = getTitle();
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title_text);

        char time_str[10];
        SysTime_GetTimeString(time_str);
        
        int time_x = sw - UI_TIME_SAFE_PAD - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, UI_TEXT_Y_TOP, time_str); 
        
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

        int count = getMenuCount();
        if (count == 0) return;

        int center_y = UI_HEADER_HEIGHT + (sh - UI_HEADER_HEIGHT) / 2; 
        
        HAL_Draw_Rect(10, center_y - 16, sw - 20, 32, 1); 
        HAL_Fill_Triangle(20, center_y - 10, 20, center_y + 10, 28, center_y, 1);

        int idx1 = (int)floor(v_pos);
        int idx2 = idx1 + 1;
        float fraction = v_pos - idx1; 
        int real_idx1 = (idx1 % count + count) % count;
        int real_idx2 = (idx2 % count + count) % count;
        
        int w1 = HAL_Get_Text_Width(getItemText(real_idx1));
        int w2 = HAL_Get_Text_Width(getItemText(real_idx2));
        float dynamic_text_w = w1 + (w2 - w1) * fraction;
        
        int block_x = center_x + (int)(dynamic_text_w / 2.0f) + 16;
        if (block_x > sw - 22) block_x = sw - 22; 
        
        HAL_Fill_Rect(block_x, center_y - 12, 8, 24, 1); 

        int base_idx = round(v_pos);
        for (int i = base_idx - 3; i <= base_idx + 3; i++) {
            float offset = i - v_pos;
            int item_y = center_y + offset * 35; 
            
            if (item_y < UI_HEADER_HEIGHT + 12 || item_y > sh - 10) continue;

            int real_idx = (i % count + count) % count;
            const char* text = getItemText(real_idx);

            int text_width = HAL_Get_Text_Width(text);
            int base_x = center_x - (text_width / 2); 
            int item_x = base_x - (offset * offset * 9.0f);  

            float distance = abs(offset);
            int final_y = item_y - 8;

            const char *p_pref = nullptr, *p_val = nullptr, *p_suff = nullptr;

            // 【架构升级】：如果这一行正在被编辑，就启用“分段渲染魔法”！
            if (real_idx == current_selection && getItemEditParts(real_idx, &p_pref, &p_val, &p_suff)) {
                drawSegmentedAnimatedText(item_x, final_y, p_pref, p_val, p_suff, distance);
            } else {
                // 普通行直接整行渲染
                HAL_Screen_ShowChineseLine_Faded(item_x, final_y, text, distance);
            }
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        current_selection = 0;
        visual_selection = 0.0f;
        onResume(); 
    }

    void onResume() override { drawMenuUI(visual_selection); }
    void onBackground() override {}

    void onLoop() override {
        bool needs_redraw = updateEditAnimation();

        int count = getMenuCount();
        if (count == 0) return;

        float target = (float)current_selection;
        float diff = target - visual_selection;

        if (diff > count / 2.0f) diff -= count;
        if (diff < -count / 2.0f) diff += count;

        if (abs(diff) > 0.01f) {
            visual_selection += diff * 0.25f; 
            while (visual_selection < 0) visual_selection += count;
            while (visual_selection >= count) visual_selection -= count;
            needs_redraw = true; 
        } else if (visual_selection != target && abs(diff) <= 0.01f) {
            visual_selection = target;
            needs_redraw = true;
        }

        if (needs_redraw) {
            drawMenuUI(visual_selection);
        }
    }

    void onDestroy() override {}
    void onKnob(int delta) override {
        int count = getMenuCount();
        if (count > 0) {
            current_selection = (current_selection + delta + count) % count;
            SYS_SOUND_GLITCH();
        }
    }
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        onItemClicked(current_selection);
    }
    void onKeyLong() override { onLongPressed(); }
};

#endif