// 文件：src/sys/sys_power.h
// 职责：电源接口：暴露电池百分比、充电状态和电池图标绘制函数。
#pragma once
#include <Arduino.h>
#include "sys/sys_constants.h"

// Compatibility aliases. The source of truth is sys_constants.h.
#ifndef PIN_BAT_ADC
#define PIN_BAT_ADC PrescriptConst::PIN_BAT_ADC
#endif
#ifndef PIN_CHRG
#define PIN_CHRG PrescriptConst::PIN_CHRG
#endif

class SysPower
{
private:
    int m_last_bat_percent = -1;
    bool m_last_bat_charging = false;
    uint32_t m_last_blink_time = 0;
    bool m_blink_state = true;
    uint32_t m_last_adc_read_time = 0;
    float m_smoothed_percent = -1.0f;

    /**
     * 功能：读取函数：返回当前对象或模块的某项状态/文本/配置。
     */
    int getRawBatteryPercent();

public:
    /**
     * 功能：初始化函数：配置模块状态、外设或运行所需缓存。
     */
    void begin();
    /**
     * 功能：功能函数：完成当前文件所属模块的一项具体逻辑。
     */
    bool isCharging();
    /**
     * 功能：读取函数：返回当前对象或模块的某项状态/文本/配置。
     */
    int getBatteryPercent();
    /**
     * 功能：功能函数：完成当前文件所属模块的一项具体逻辑。
     */
    bool needsRedraw();
    /**
     * 功能：绘制函数：把当前状态转换为屏幕上的文字、线条或动画帧。
     */
    void drawBatteryIcon(int32_t x, int32_t y);
};

extern SysPower sysPower;
