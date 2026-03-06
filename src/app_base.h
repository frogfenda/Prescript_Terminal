// 文件：src/app_base.h
#ifndef __APP_BASE_H
#define __APP_BASE_H

#include <Arduino.h>
#include "hal.h"

class AppBase {
public:
    virtual ~AppBase() {}

    // === 生命周期 ===
    virtual void onCreate() = 0;
    
    // 【新增】：当从下级页面退回来时，调用唤醒（而不是重新创建）
    virtual void onResume() {}     
    
    // 【新增】：当被下级页面覆盖时，转入后台（而不是直接销毁）
    virtual void onBackground() {} 
    
    virtual void onLoop() = 0;
    virtual void onDestroy() = 0;

    // === 交互事件 ===
    virtual void onKnob(int delta) = 0;
    virtual void onKeyShort() = 0;
    virtual void onKeyLong() = 0;
};

#endif