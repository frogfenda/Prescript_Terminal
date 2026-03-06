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
    void drawAppWindow(const char* title) {
        HAL_Screen_DrawHeader();

        int sw = HAL_Get_Screen_Width();
        
        // 【净化】：全部使用 UI 宏定义！
        HAL_Screen_ShowChineseLine(UI_MARGIN_LEFT, UI_TEXT_Y_TOP, title);
        
        char time_str[10];
        SysTime_GetTimeString(time_str);
        
        int time_x = sw - UI_TIME_SAFE_PAD - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, UI_TEXT_Y_TOP, time_str);
        
        // 分割线画满全屏
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);
    }
};

#endif