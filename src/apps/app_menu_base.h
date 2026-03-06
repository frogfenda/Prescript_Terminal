// 文件：src/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "app_base.h"
#include "app_manager.h"
#include <time.h> // 【新增】：引入 ESP32 的内置时间库

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
        
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        int header_h = 38; 
        int center_x = sw / 2; 

        const char* title_text = getTitle();
        HAL_Screen_ShowChineseLine(20, 16, title_text);

        // 【架构升级】：统一通过接口提取时间
        char time_str[10];
        SysTime_GetTimeString(time_str);
        
        int time_x = sw - 28 - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, 16, time_str); 
        
        HAL_Draw_Line(0, header_h, sw, header_h, 1);

        int count = getMenuCount();
        if (count == 0) return;

        int center_y = header_h + (sh - header_h) / 2; 
        
        HAL_Draw_Rect(10, center_y - 16, sw - 20, 32, 1); 
        HAL_Fill_Triangle(20, center_y - 10, 20, center_y + 10, 28, center_y, 1);
        // ==========================================
        // 【新增黑科技：平滑跟随的呼吸动态色块】
        // ==========================================
        // 1. 获取当前滚动进度处于哪两个选项之间
        int idx1 = (int)floor(v_pos);
        int idx2 = idx1 + 1;
        float fraction = v_pos - idx1; // 小数部分，代表两个选项间的滚动进度
        
        // 2. 环形折叠，算出真实的菜单索引
        int real_idx1 = (idx1 % count + count) % count;
        int real_idx2 = (idx2 % count + count) % count;
        
        // 3. 获取这两个选项的物理像素宽度
        int w1 = HAL_Get_Text_Width(getItemText(real_idx1));
        int w2 = HAL_Get_Text_Width(getItemText(real_idx2));
        
        // 4. 核心：通过线性插值，计算出当前动画帧下，绝对平滑的过度宽度
        float dynamic_text_w = w1 + (w2 - w1) * fraction;
        
        // 5. 将实色块的 X 坐标死死咬住文字右边缘（中心点 + 文字一半 + 16像素安全间距）
        int block_x = center_x + (int)(dynamic_text_w / 2.0f) + 16;
        
        // 6. 极限保护：防止文字过长导致色块直接飞出外框边缘
        if (block_x > sw - 22) block_x = sw - 22; 
        
        // 画出这个具有生命力的跟随实色块！
        HAL_Fill_Rect(block_x, center_y - 12, 8, 24, 1); 
        // ==========================================

        int base_idx = round(v_pos);
        for (int i = base_idx - 3; i <= base_idx + 3; i++) {
            
            float offset = i - v_pos;
            int item_y = center_y + offset * 35; 
            
            if (item_y < header_h + 12 || item_y > sh - 10) continue;

            int real_idx = (i % count + count) % count;
            const char* text = getItemText(real_idx);

            int text_width = HAL_Get_Text_Width(text);
            int base_x = center_x - (text_width / 2); 
            
            int item_x = base_x - (offset * offset * 9.0f);  

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