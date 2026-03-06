// 文件：src/sys/app_base.h
#ifndef __APP_BASE_H
#define __APP_BASE_H

#include <Arduino.h>
#include "hal.h"
#include "sys_time.h"

class AppBase {
public:
    virtual ~AppBase() {}

    virtual void onCreate() = 0;
    virtual void onResume() {}     
    virtual void onBackground() {} 
    virtual void onLoop() = 0;
    virtual void onDestroy() = 0;

    virtual void onKnob(int delta) = 0;
    virtual void onKeyShort() = 0;
    virtual void onKeyLong() = 0;

protected:
    float edit_anim_progress = 0.0f; 
    int   edit_anim_dir = 0;         
    uint32_t edit_anim_last_tick = 0; // 【新增】：用于动画锁帧的时间戳

    void triggerEditAnimation(int delta) {
        edit_anim_dir = delta > 0 ? 1 : -1;
        edit_anim_progress = 1.0f; 
        edit_anim_last_tick = millis();
    }

    bool updateEditAnimation() {
        if (edit_anim_progress > 0) {
            uint32_t now = millis();
            // 【核心修复 1】：锁定动画刷新率在约 60FPS (每 16ms 走一帧)
            // 彻底解决屏幕疯狂重绘导致的“频闪”和“撕裂”！
            if (now - edit_anim_last_tick >= 16) { 
                edit_anim_progress -= 0.15f; 
                edit_anim_last_tick = now;
                if (edit_anim_progress <= 0) {
                    edit_anim_progress = 0;
                    edit_anim_dir = 0;
                }
                return true; // 只有在进入新的一帧时，才允许屏幕重绘
            }
        }
        return false;
    }

    // 【核心修复 2】：分段渲染器。把一句话拆成 前缀、变量(跳动)、后缀 三段来画
    void drawSegmentedAnimatedText(int x, int y, const char* prefix, const char* anim_val, const char* suffix, float distance = 0.0f) {
        int current_x = x;
        
        // 1. 画前缀 (稳如泰山)
        if (prefix && prefix[0] != '\0') {
            if (distance > 0.01f) HAL_Screen_ShowChineseLine_Faded(current_x, y, prefix, distance);
            else HAL_Screen_ShowChineseLine(current_x, y, prefix);
            current_x += HAL_Get_Text_Width(prefix);
        }
        
        // 2. 画核心变量 (只有它会跳动和残影！)
        if (anim_val && anim_val[0] != '\0') {
            int anim_y = y;
            float anim_dist = distance;
            if (edit_anim_progress > 0.01f) {
                anim_y += (int)(edit_anim_progress * edit_anim_dir * 12.0f);
                anim_dist += edit_anim_progress * 1.5f;
            }
            if (anim_dist > 0.01f) HAL_Screen_ShowChineseLine_Faded(current_x, anim_y, anim_val, anim_dist);
            else HAL_Screen_ShowChineseLine(current_x, anim_y, anim_val);
            
            current_x += HAL_Get_Text_Width(anim_val);
        }
        
        // 3. 画后缀 (稳如泰山)
        if (suffix && suffix[0] != '\0') {
            if (distance > 0.01f) HAL_Screen_ShowChineseLine_Faded(current_x, y, suffix, distance);
            else HAL_Screen_ShowChineseLine(current_x, y, suffix);
        }
    }

    void drawAppWindow(const char* title) {
        HAL_Screen_DrawHeader();

        int sw = HAL_Get_Screen_Width();
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title);
        
        char time_str[10];
        SysTime_GetTimeString(time_str);
        
        int time_x = sw - UI_TIME_SAFE_PAD - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, UI_TEXT_Y_TOP, time_str);
        
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);
    }
};

#endif