// 文件：src/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "app_base.h"
#include "app_manager.h"

class AppMenuBase : public AppBase {
protected:
    int current_selection;
    float visual_selection;

    virtual int getMenuCount() = 0;                  
    virtual const char* getTitle() = 0;              
    virtual const char* getItemText(int index) = 0;  
    virtual void onItemClicked(int index) = 0;       
    virtual void onLongPressed() = 0;                

    void drawMenuUI(float v_pos) {
        HAL_Sprite_Clear();
        
        HAL_Screen_ShowChineseLine(70, 16, getTitle());
        HAL_Draw_Line(20, 38, 220, 38, 1);

        int count = getMenuCount();
        if (count == 0) return;

        int center_y = 120; 
        HAL_Draw_Rect(10, center_y - 16, 220, 32, 1); 
        HAL_Fill_Triangle(20, center_y - 10, 20, center_y + 10, 28, center_y, 1); 
        HAL_Fill_Rect(190, center_y - 12, 8, 24, 1);

        for (int i = 0; i < count; i++) {
            float offset = i - v_pos;
            
            if (offset > count / 2.0f) offset -= count;
            else if (offset < -count / 2.0f) offset += count;

            int item_y = center_y + offset * 35; 
            
            if (item_y < 50 || item_y > 230) continue;

            const char* text = getItemText(i);

            // 【优化1：绝对居中】获取当前文本像素宽度，让它永远乖乖待在中间
            int text_width = HAL_Get_Text_Width(text);
            int base_x = 120 - (text_width / 2); // 240 屏幕宽度的一半是 120
            
            // 【优化2：二次方抛物线圆弧】让弧度极其明显（offset * offset）
            int item_x = base_x - (offset * offset * 9.0f);  

            // 【优化3：3D 景深淡出】调用我们新写的真彩色渐变函数
            float distance = abs(offset);
            HAL_Screen_ShowChineseLine_Faded(item_x, item_y - 8, text, distance);
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
            drawMenuUI(visual_selection);
        } else if (visual_selection != target && abs(diff) <= 0.01f) {
            visual_selection = target;
            drawMenuUI(visual_selection);
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override {
        int count = getMenuCount();
        if (count > 0) {
            current_selection = (current_selection + delta + count) % count;
            HAL_Buzzer_Random_Glitch();
        }
    }

    void onKeyShort() override {
        HAL_Buzzer_Play_Tone(2500, 80);
        onItemClicked(current_selection);
    }
    void onKeyLong() override { onLongPressed(); }
};

#endif