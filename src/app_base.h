// 文件：src/app_base.h
#ifndef __APP_BASE_H
#define __APP_BASE_H

#include <Arduino.h>
#include "hal.h"


class AppBase {
public:
    virtual ~AppBase() {}

    // 生命周期
    virtual void onCreate() = 0;   // 应用启动时调一次（画初始界面）
    virtual void onLoop() = 0;     // 每隔几毫秒调一次（刷动画、逻辑）
    virtual void onDestroy() = 0;  // 应用退出时调一次（释放内存）

    // 交互事件
    virtual void onKnob(int delta) = 0; // 旋钮转动
    virtual void onKeyShort() = 0;      // 按钮短按
    virtual void onKeyLong() = 0;       // 按钮长按
};

#endif