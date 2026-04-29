#pragma once
#include <Arduino.h>

// 把原本在 hal.h 里的引脚定义搬过来
#define PIN_BAT_ADC 8
#define PIN_CHRG    16

class SysPower {
private:
    int m_last_bat_percent = -1;
    bool m_last_bat_charging = false;
    uint32_t m_last_blink_time = 0;
    bool m_blink_state = true;
    uint32_t m_last_adc_read_time = 0;
    float m_smoothed_percent = -1.0f;

    int getRawBatteryPercent();

public:
    void begin();
    bool isCharging();
    int getBatteryPercent();
    bool needsRedraw();
    void drawBatteryIcon(int32_t x, int32_t y);
};

extern SysPower sysPower;