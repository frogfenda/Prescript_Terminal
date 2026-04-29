#include "sys_power.h"
#include "../hal/hal.h" // 需要调用底层的画图函数

SysPower sysPower;

void SysPower::begin() {
    pinMode(PIN_BAT_ADC, ANALOG);
    pinMode(PIN_CHRG, INPUT_PULLUP);
}

bool SysPower::isCharging() {
    return digitalRead(PIN_CHRG) == LOW;
}

int SysPower::getRawBatteryPercent() {
    int adc_mv = analogReadMilliVolts(PIN_BAT_ADC);
    int bat_mv = adc_mv * 2; 

    if (isCharging()) { 
        bat_mv -= 200; // 充电压降补偿
    }

    int target_percent = (bat_mv - 3000) * 100 / (4100 - 3000);
    if (target_percent > 100) target_percent = 100;
    if (target_percent < 0) target_percent = 0;
    
    if (m_smoothed_percent < 0) {
        m_smoothed_percent = target_percent; 
    } else {
        m_smoothed_percent = (m_smoothed_percent * 0.80f) + (target_percent * 0.20f);
    }
    return (int)m_smoothed_percent;
}

int SysPower::getBatteryPercent() {
    return (m_last_bat_percent == -1) ? 100 : m_last_bat_percent;
}

bool SysPower::needsRedraw() {
    bool needs_update = false;
    uint32_t now = millis();

    // 1000ms 节流阀
    if (now - m_last_adc_read_time > 1000 || m_last_adc_read_time == 0) {
        m_last_adc_read_time = now;
        
        int current_percent = getRawBatteryPercent();
        bool current_charging = isCharging();

        if (current_percent != m_last_bat_percent || current_charging != m_last_bat_charging) {
            m_last_bat_percent = current_percent;
            m_last_bat_charging = current_charging;
            needs_update = true;
        }
    }

    if (m_last_bat_charging) {
        if (now - m_last_blink_time > 500) {
            m_blink_state = !m_blink_state;
            m_last_blink_time = now;
            needs_update = true; 
        }
    } else {
        m_blink_state = true;
    }

    return needs_update;
}

void SysPower::drawBatteryIcon(int32_t x, int32_t y) {
    int percent = getBatteryPercent();
    bool charging = m_last_bat_charging;

    uint16_t color_frame = TFT_WHITE; 
    uint16_t color_fill  = (percent > 20 || charging) ? TFT_CYAN : TFT_RED; 
    
    HAL_Draw_Rect(x, y, 22, 10, color_frame);       
    HAL_Fill_Rect(x + 22, y + 3, 2, 4, color_frame); 
    HAL_Fill_Rect(x + 1, y + 1, 20, 8, TFT_BLACK);

    int blocks = (percent + 10) / 20; 
    if (percent > 0 && blocks == 0) blocks = 1; 

    for (int i = 0; i < blocks; i++) {
        HAL_Fill_Rect(x + 2 + (i * 4), y + 2, 3, 6, color_fill);
    }

    if (charging && m_blink_state) {
        int lx = x + 26;
        int ly = y + 1;
        uint16_t bolt_color = 0xFFE0; 
        
        HAL_Draw_Line(lx + 2, ly,     lx,     ly + 4, bolt_color);
        HAL_Draw_Line(lx,     ly + 4, lx + 3, ly + 4, bolt_color);
        HAL_Draw_Line(lx + 3, ly + 4, lx + 1, ly + 8, bolt_color);
    }
}