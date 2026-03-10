// 文件：src/sys/sys_config.cpp
#include "sys_config.h"
#include <Preferences.h>

SysConfig sysConfig;
Preferences prefs;

void SysConfig::load() {
    prefs.begin("terminal_cfg", false);
    
    wifi_ssid = prefs.getString("ssid", "Your_WiFi_Name"); 
    wifi_pass = prefs.getString("pass", "12345678");
    language = prefs.getUChar("lang", 1);             
    sleep_time_ms = prefs.getUInt("sleep", 30000);    
    decode_anim_style = prefs.getUChar("anim_s", 0); // 【新增】：加载动画样式

    auto_push_enable = prefs.getBool("p_en", false);
    auto_push_min_min = prefs.getUInt("p_min", 30);
    auto_push_max_min = prefs.getUInt("p_max", 120);
    
    pomodoro_current_idx = prefs.getUChar("pm_idx", 0);
    if (pomodoro_current_idx > 4) pomodoro_current_idx = 0;

    const char* def_names[5] = {"常规专注", "深度工作", "短时冲刺", "阅读模式", "冥想休息"};
    uint32_t def_w[5] = {25, 60, 15, 45, 10};
    uint32_t def_r[5] = {5, 10, 3, 10, 5};
    for (int i = 0; i < 5; i++) {
        pomodoro_presets[i].name = prefs.getString(("pm_n_" + String(i)).c_str(), def_names[i]);
        pomodoro_presets[i].work_min = prefs.getUInt(("pm_w_" + String(i)).c_str(), def_w[i]);
        pomodoro_presets[i].rest_min = prefs.getUInt(("pm_r_" + String(i)).c_str(), def_r[i]);
    }
    
    // 【新增】：加载闹钟列表
    alarm_count = prefs.getUChar("al_cnt", 0);
    if (alarm_count > 10) alarm_count = 10;
    for (int i = 0; i < alarm_count; i++) {
        alarms[i].is_active = prefs.getBool(("al_en_" + String(i)).c_str(), false);
        alarms[i].hour = prefs.getUChar(("al_h_" + String(i)).c_str(), 8);
        alarms[i].min = prefs.getUChar(("al_m_" + String(i)).c_str(), 0);
        alarms[i].name = prefs.getString(("al_n_" + String(i)).c_str(), "闹钟" + String(i+1));
        alarms[i].prescript = prefs.getString(("al_p_" + String(i)).c_str(), "时间已到，立即切断休眠执行唤醒。");
    }

    schedule_count = prefs.getUChar("sc_cnt", 0);
    if (schedule_count > 15) schedule_count = 15;
    for (int i = 0; i < schedule_count; i++) {
        schedules[i].target_time = prefs.getUInt(("sc_tt_" + String(i)).c_str(), 0);
        schedules[i].expire_time = prefs.getUInt(("sc_et_" + String(i)).c_str(), 0);
        schedules[i].title = prefs.getString(("sc_tl_" + String(i)).c_str(), "待办");
        schedules[i].prescript = prefs.getString(("sc_ps_" + String(i)).c_str(), "");
        schedules[i].is_expired = prefs.getBool(("sc_ex_" + String(i)).c_str(), false);
        schedules[i].is_restored = prefs.getBool(("sc_rs_" + String(i)).c_str(), false);
    }
    custom_prescript_count = prefs.getInt("cp_cnt", 0);
    if (custom_prescript_count > MAX_CUSTOM_PRESCRIPTS) custom_prescript_count = MAX_CUSTOM_PRESCRIPTS;
    for (int i = 0; i < custom_prescript_count; i++) {
        char key[16]; sprintf(key, "cp_%d", i);
        custom_prescripts[i] = prefs.getString(key, "");
    }
    prefs.end();
}

void SysConfig::save() {
    prefs.begin("terminal_cfg", false);
    
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("pass", wifi_pass);
    prefs.putUChar("lang", language);
    prefs.putUInt("sleep", sleep_time_ms);
    prefs.putUChar("anim_s", decode_anim_style);
    
    prefs.putBool("p_en", auto_push_enable);
    prefs.putUInt("p_min", auto_push_min_min);
    prefs.putUInt("p_max", auto_push_max_min);
    
    prefs.putUChar("pm_idx", pomodoro_current_idx);
    for (int i = 0; i < 5; i++) {
        prefs.putString(("pm_n_" + String(i)).c_str(), pomodoro_presets[i].name);
        prefs.putUInt(("pm_w_" + String(i)).c_str(), pomodoro_presets[i].work_min);
        prefs.putUInt(("pm_r_" + String(i)).c_str(), pomodoro_presets[i].rest_min);
    }
    
    // 【新增】：保存闹钟列表
    prefs.putUChar("al_cnt", alarm_count);
    for (int i = 0; i < alarm_count; i++) {
        prefs.putBool(("al_en_" + String(i)).c_str(), alarms[i].is_active);
        prefs.putUChar(("al_h_" + String(i)).c_str(), alarms[i].hour);
        prefs.putUChar(("al_m_" + String(i)).c_str(), alarms[i].min);
        prefs.putString(("al_n_" + String(i)).c_str(), alarms[i].name);
        prefs.putString(("al_p_" + String(i)).c_str(), alarms[i].prescript);
    }
    
    prefs.putUChar("sc_cnt", schedule_count);
    for (int i = 0; i < schedule_count; i++) {
        prefs.putUInt(("sc_tt_" + String(i)).c_str(), schedules[i].target_time);
        prefs.putUInt(("sc_et_" + String(i)).c_str(), schedules[i].expire_time);
        prefs.putString(("sc_tl_" + String(i)).c_str(), schedules[i].title);
        prefs.putString(("sc_ps_" + String(i)).c_str(), schedules[i].prescript);
        prefs.putBool(("sc_ex_" + String(i)).c_str(), schedules[i].is_expired);
        prefs.putBool(("sc_rs_" + String(i)).c_str(), schedules[i].is_restored);
    }
    prefs.putInt("cp_cnt", custom_prescript_count);
    for (int i = 0; i < custom_prescript_count; i++) {
        char key[16]; sprintf(key, "cp_%d", i);
        prefs.putString(key, custom_prescripts[i]);
    }
    
    prefs.end();
}