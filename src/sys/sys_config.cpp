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

    // 【新增】：加载推送设置（默认关闭，30~120分钟）
    auto_push_enable = prefs.getBool("p_en", false);
    auto_push_min_min = prefs.getUInt("p_min", 30);
    auto_push_max_min = prefs.getUInt("p_max", 120);
    
    prefs.end();
}

void SysConfig::save() {
    prefs.begin("terminal_cfg", false);
    
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("pass", wifi_pass);
    prefs.putUChar("lang", language);
    prefs.putUInt("sleep", sleep_time_ms);

    // 【新增】：保存推送设置
    prefs.putBool("p_en", auto_push_enable);
    prefs.putUInt("p_min", auto_push_min_min);
    prefs.putUInt("p_max", auto_push_max_min);
    
    prefs.end();
}