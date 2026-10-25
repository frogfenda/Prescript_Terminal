// 文件：src/sys/sys_power.h
// 职责：电源接口：暴露电池百分比、充电状态和供 UI 使用的只读显示状态。
#pragma once
#include <Arduino.h>

struct SysPowerDisplayState
{
    int battery_percent = 100;
    bool charging = false;
    bool charging_indicator_on = false;
};

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
    int getBatteryPercent() const;
    /**
     * 功能：功能函数：完成当前文件所属模块的一项具体逻辑。
     */
    bool needsRedraw();
    /** 返回当前电池显示所需的只读状态；绘制由 UI 层拥有。 */
    SysPowerDisplayState getDisplayState() const;
};

extern SysPower sysPower;
