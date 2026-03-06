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

    auto_push_enable = prefs.getBool("p_en", false);
    auto_push_min_min = prefs.getUInt("p_min", 30);
    auto_push_max_min = prefs.getUInt("p_max", 120);
    
    // 【新增】：加载番茄钟预设
    pomodoro_current_idx = prefs.getUChar("pm_idx", 0);
    if (pomodoro_current_idx > 4) pomodoro_current_idx = 0;

    // 默认的5个出厂预设参数
    const char* def_names[5] = {"常规专注", "深度工作", "短时冲刺", "阅读模式", "冥想休息"};
    uint32_t def_w[5] = {25, 60, 15, 45, 10};
    uint32_t def_r[5] = {5, 10, 3, 10, 5};

    for (int i = 0; i < 5; i++) {
        String key_n = "pm_n_" + String(i);
        String key_w = "pm_w_" + String(i);
        String key_r = "pm_r_" + String(i);

        pomodoro_presets[i].name = prefs.getString(key_n.c_str(), def_names[i]);
        pomodoro_presets[i].work_min = prefs.getUInt(key_w.c_str(), def_w[i]);
        pomodoro_presets[i].rest_min = prefs.getUInt(key_r.c_str(), def_r[i]);
    }
    
    prefs.end();
}

void SysConfig::save() {
    prefs.begin("terminal_cfg", false);
    
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("pass", wifi_pass);
    prefs.putUChar("lang", language);
    prefs.putUInt("sleep", sleep_time_ms);

    prefs.putBool("p_en", auto_push_enable);
    prefs.putUInt("p_min", auto_push_min_min);
    prefs.putUInt("p_max", auto_push_max_min);
    
    // 【新增】：保存番茄钟预设
    prefs.putUChar("pm_idx", pomodoro_current_idx);
    for (int i = 0; i < 5; i++) {
        String key_n = "pm_n_" + String(i);
        String key_w = "pm_w_" + String(i);
        String key_r = "pm_r_" + String(i);

        prefs.putString(key_n.c_str(), pomodoro_presets[i].name);
        prefs.putUInt(key_w.c_str(), pomodoro_presets[i].work_min);
        prefs.putUInt(key_r.c_str(), pomodoro_presets[i].rest_min);
    }
    
    prefs.end();
}