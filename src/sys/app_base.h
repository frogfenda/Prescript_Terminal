// 文件：src/sys/app_base.h
#ifndef __APP_BASE_H
#define __APP_BASE_H

#include <Arduino.h>
#include "hal.h"
#include "sys_time.h" // 【修改】：引入我们自己的时间引擎

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
        HAL_Screen_ShowChineseLine(20, 16, title);
        
        // 【架构升级】：直接通过统一接口获取时间字符串，不再写恶心的逻辑！
        char time_str[10];
        SysTime_GetTimeString(time_str);
        
        int time_x = sw - 28 - HAL_Get_Text_Width(time_str);
        HAL_Screen_ShowTextLine(time_x, 16, time_str);
        
        HAL_Draw_Line(0, 38, sw, 38, 1);
    }
};

#endif