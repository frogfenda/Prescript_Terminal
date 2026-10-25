/*
【模块职责】电源状态实现。对 ADC 电压做百分比换算和平滑缓存，电量变化达到阈值时通知 HUD 重绘。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys/sys_power.h"
#include "bsp/bsp_power.h"

SysPower sysPower;

void SysPower::begin() {
    BSP::Power::BeginBatterySense();
}

bool SysPower::isCharging() {
    return BSP::Power::IsCharging();
}

int SysPower::getRawBatteryPercent() {
    int bat_mv = BSP::Power::ReadBatteryMilliVolts();

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

int SysPower::getBatteryPercent() const {
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

SysPowerDisplayState SysPower::getDisplayState() const
{
    SysPowerDisplayState state;
    state.battery_percent = getBatteryPercent();
    state.charging = m_last_bat_charging;
    state.charging_indicator_on = m_blink_state;
    return state;
}

