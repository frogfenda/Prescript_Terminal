#include "sys_config.h"
#include <Preferences.h>

SysConfig sysConfig;
Preferences prefs;

void SysConfig::load() {
    prefs.begin("terminal_cfg", false);
    
    // 如果是第一次开机（硬盘里没数据），就加载后面的默认值
    wifi_ssid = prefs.getString("ssid", "qingwafenda"); 
    wifi_pass = prefs.getString("pass", "frogfenda");
    language = prefs.getUChar("lang", 1);             // 默认中文
    sleep_time_ms = prefs.getUInt("sleep", 30000);    // 默认30秒休眠
    
    prefs.end();
}

void SysConfig::save() {
    prefs.begin("terminal_cfg", false);
    
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("pass", wifi_pass);
    prefs.putUChar("lang", language);
    prefs.putUInt("sleep", sleep_time_ms);
    
    prefs.end();
}