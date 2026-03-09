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

    virtual bool getItemEditParts(int index, const char** prefix, const char** anim_val, const char** suffix) {
        return false;
    }
    
    virtual uint16_t getItemColor(int index) {
        return TFT_CYAN;
    }

    void drawMenuUI(float v_pos) {
        HAL_Sprite_Clear(); 
        
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // ==========================================
        // 1. 左侧：静态 HUD 信息面板
        // ==========================================
        int left_panel_w = 104; // 划定左侧面板宽度

        // 垂直居中偏上：绘制系统抬头
        const char* title_text = getTitle();
        int title_w = HAL_Get_Text_Width(title_text);
        int title_x = (left_panel_w - title_w) / 2;
        if(title_x < 2) title_x = 2; // 防止字太长顶到边缘
        HAL_Screen_ShowChineseLine(title_x, sh / 2 - 22, title_text);

        // 垂直居中偏下：绘制数字时钟
        char time_str[10];
        SysTime_GetTimeString(time_str);
        int time_w = HAL_Get_Text_Width(time_str);
        int time_x = (left_panel_w - time_w) / 2;
        HAL_Screen_ShowTextLine(time_x, sh / 2 + 6, time_str); 
        
        // 分割装甲线：切断左右视觉
        HAL_Draw_Line(left_panel_w, 8, left_panel_w, sh - 8, 1);

        // ==========================================
        // 2. 右侧：3D 滚轴核心阵列
        // ==========================================
        int right_panel_x = left_panel_w;
        int right_panel_w = sw - left_panel_w;
        int center_x = right_panel_x + right_panel_w / 2; 
        int center_y = sh / 2; // 滚轮绝对居中

        int real_count = getMenuCount();
        if (real_count == 0) return;
        int count = (real_count == 2) ? 4 : real_count;

        // 绘制扫描锁定框 (收缩至右侧区域)
        HAL_Draw_Rect(right_panel_x + 6, center_y - 14, right_panel_w - 12, 28, 1); 
        HAL_Fill_Triangle(right_panel_x + 12, center_y - 6, right_panel_x + 12, center_y + 6, right_panel_x + 18, center_y, 1);

        int idx1 = (int)floor(v_pos);
        int idx2 = idx1 + 1;
        float fraction = v_pos - idx1; 
        
        int logical_idx1 = (idx1 % count + count) % count;
        int logical_idx2 = (idx2 % count + count) % count;
        int real_idx1 = logical_idx1 % real_count;
        int real_idx2 = logical_idx2 % real_count;
        
        int w1 = HAL_Get_Text_Width(getItemText(real_idx1));
        int w2 = HAL_Get_Text_Width(getItemText(real_idx2));
        float dynamic_text_w = w1 + (w2 - w1) * fraction;
        
        // 动态尾随光标块
        int block_x = center_x + (int)(dynamic_text_w / 2.0f) + 12;
        if (block_x > sw - 12) block_x = sw - 12; 
        HAL_Fill_Rect(block_x, center_y - 10, 6, 20, 1); 

        int base_idx = round(v_pos);
        
        // 【极限视口渲染】：高度只有 76，循环上下各渲染 2 个就足够了
        for (int i = base_idx - 2; i <= base_idx + 2; i++) {
            float offset = i - v_pos;
            // 【关键】：缩小条目行距为 26，以完美嵌合 76 像素高的极限视野
            int item_y = center_y + offset * 26; 
            
            // 视口裁剪：超出上下边缘直接丢弃，不浪费算力
            if (item_y < -10 || item_y > sh + 10) continue;

            int logical_idx = (i % count + count) % count;
            int real_idx = logical_idx % real_count;
            const char* text = getItemText(real_idx);

            int text_width = HAL_Get_Text_Width(text);
            int base_x = center_x - (text_width / 2); 
            // 【关键】：减弱 3D 轮盘的曲率，防止顶撞左侧分割线
            int item_x = base_x - (offset * offset * 4.0f);  

            float distance = abs(offset);
            int final_y = item_y - 8;

            const char *p_pref = nullptr, *p_val = nullptr, *p_suff = nullptr;

            if (logical_idx == current_selection && getItemEditParts(real_idx, &p_pref, &p_val, &p_suff)) {
                drawSegmentedAnimatedText(item_x, final_y, p_pref, p_val, p_suff, distance);
            } else {
                uint16_t item_color = getItemColor(real_idx);
                HAL_Screen_ShowChineseLine_Faded_Color(item_x, final_y, text, distance, item_color);
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

        int real_count = getMenuCount();
        if (real_count == 0) return;
        int count = (real_count == 2) ? 4 : real_count;

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
        int real_count = getMenuCount();
        if (real_count > 0) {
            int count = (real_count == 2) ? 4 : real_count;
            current_selection = (current_selection + delta + count) % count;
            SYS_SOUND_GLITCH();
        }
    }
    
    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        int real_count = getMenuCount();
        if (real_count > 0) {
            onItemClicked(current_selection % real_count);
        }
    }
    
    void onKeyLong() override { onLongPressed(); }
};

#endif