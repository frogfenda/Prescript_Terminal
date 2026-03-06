// 文件：src/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "app_base.h"
#include "app_manager.h"


class AppMenuBase : public AppBase {
protected:
    int current_selection;
    float visual_selection;

    // === 留给子类的“填空题”（纯虚函数） ===
    virtual int getMenuCount() = 0;                  // 1. 有几个选项？
    virtual const char* getTitle() = 0;              // 2. 菜单标题叫啥？
    virtual const char* getItemText(int index) = 0;  // 3. 第 index 个选项的文字是啥？
    virtual void onItemClicked(int index) = 0;       // 4. 点了第 index 个选项要干嘛？
    virtual void onLongPressed() = 0;                // 5. 长按要干嘛？

    // === 通用菜单渲染引擎（内置了排版和动画） ===
    void drawMenuUI(float v_pos) {
        HAL_Sprite_Clear();
        HAL_Screen_ShowChineseLine(70, 16, getTitle());
        HAL_Draw_Line(20, 38, 220, 38, 1);

        int count = getMenuCount();
        int start_y = (count <= 3) ? 65 : 52; // 选项少就居中一点
        int spacing = (count <= 3) ? 40 : 30;

        for (int i = 0; i < count; i++) {
            HAL_Screen_ShowChineseLine(40, start_y + i * spacing - 2, getItemText(i));
        }

        float draw_y = start_y + v_pos * spacing;
        HAL_Draw_Rect(10, (int)draw_y - 4, 220, 26, 1); 
        HAL_Fill_Triangle(20, (int)draw_y, 20, (int)draw_y + 14, 27, (int)draw_y + 7, 1); 
        HAL_Fill_Rect(190, (int)draw_y + 2, 8, 14, 1);

        HAL_Screen_Update();
    }

public:
    // 下面的生命周期和交互逻辑，子类永远不需要再写了！
    void onCreate() override {
        current_selection = 0;
        visual_selection = 0.0f;
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        drawMenuUI(visual_selection);
    }

    void onLoop() override {
        float target = (float)current_selection;
        float diff = target - visual_selection;
        if (abs(diff) > 0.01f) {
            if (abs(diff) > 1.5f) visual_selection = target;
            else visual_selection += diff * 0.25f; 
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