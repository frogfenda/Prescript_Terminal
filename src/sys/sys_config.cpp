// 文件：src/sys/sys_config.cpp
#include "sys_config.h"
#include "sys_fs.h"       // 引入我们上一版的 LittleFS 中枢
#include <ArduinoJson.h>  // 引入 JSON 引擎

SysConfig sysConfig;

const char* CONFIG_FILE = "/assets/config.json";// 配置文件在硬盘上的绝对路径

void SysConfig::load() {
    // 1. 从 LittleFS 硬盘读取 JSON 文本
    String json = SysFS_Read_File(CONFIG_FILE);
    
    // 2. 准备 JSON 解析引擎 (ArduinoJson 7 会自动管理内存)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    // ==========================================
    // 如果读取失败、文件不存在、或者格式错误，赋予出厂默认值
    // ==========================================
    if (error || json == "") {
        Serial.println("[CONFIG] 配置文件无效或不存在，加载出厂默认协议...");
        
        wifi_ssid = "Your_WiFi_Name";
        wifi_pass = "12345678";
        language = 1;             
        sleep_time_ms = 30000;    
        decode_anim_style = 0;

        auto_push_enable = false;
        auto_push_min_min = 30;
        auto_push_max_min = 120;
        
        pomodoro_current_idx = 0;
        const char* def_names[5] = {"常规专注", "深度工作", "短时冲刺", "阅读模式", "冥想休息"};
        uint32_t def_w[5] = {25, 60, 15, 45, 10};
        uint32_t def_r[5] = {5, 10, 3, 10, 5};
        for (int i = 0; i < 5; i++) {
            pomodoro_presets[i].name = def_names[i];
            pomodoro_presets[i].work_min = def_w[i];
            pomodoro_presets[i].rest_min = def_r[i];
        }
        
        alarm_count = 0;
        schedule_count = 0;
        custom_prescript_count = 0;

        // 生成默认文件并保存
        save();
        return;
    }

    // ==========================================
    // 从 JSON 树中精准解析数据 (带默认值回退机制)
    // ==========================================
    wifi_ssid = doc["wifi_ssid"] | "Your_WiFi_Name";
    wifi_pass = doc["wifi_pass"] | "12345678";
    language = doc["language"] | 1;
    sleep_time_ms = doc["sleep_time_ms"] | 30000;
    decode_anim_style = doc["decode_anim_style"] | 0;

    auto_push_enable = doc["auto_push_enable"] | false;
    auto_push_min_min = doc["auto_push_min_min"] | 30;
    auto_push_max_min = doc["auto_push_max_min"] | 120;

    pomodoro_current_idx = doc["pom_idx"] | 0;
    JsonArray pm_arr = doc["pom_presets"];
    for (int i = 0; i < 5; i++) {
        pomodoro_presets[i].name = pm_arr[i]["n"] | "预设";
        pomodoro_presets[i].work_min = pm_arr[i]["w"] | 25;
        pomodoro_presets[i].rest_min = pm_arr[i]["r"] | 5;
    }

    alarm_count = doc["alarm_count"] | 0;
    JsonArray al_arr = doc["alarms"];
    for (int i = 0; i < alarm_count; i++) {
        alarms[i].is_active = al_arr[i]["en"] | false;
        alarms[i].hour = al_arr[i]["h"] | 8;
        alarms[i].min = al_arr[i]["m"] | 0;
        alarms[i].name = al_arr[i]["n"] | "闹钟";
        alarms[i].prescript = al_arr[i]["p"] | "唤醒指令";
    }

    schedule_count = doc["schedule_count"] | 0;
    JsonArray sc_arr = doc["schedules"];
    for (int i = 0; i < schedule_count; i++) {
        schedules[i].target_time = sc_arr[i]["tt"] | 0;
        schedules[i].expire_time = sc_arr[i]["et"] | 0;
        schedules[i].title = sc_arr[i]["tl"] | "待办";
        schedules[i].prescript = sc_arr[i]["ps"] | "";
        schedules[i].is_expired = sc_arr[i]["ex"] | false;
        schedules[i].is_restored = sc_arr[i]["rs"] | false;
    }

    custom_prescript_count = doc["cp_count"] | 0;
    JsonArray cp_arr = doc["custom_p"];
    for (int i = 0; i < custom_prescript_count; i++) {
        custom_prescripts[i] = cp_arr[i] | "";
    }
}

void SysConfig::save() {
    JsonDocument doc;

    // ==========================================
    // 将内存数据打包成极其规整的 JSON 树
    // ==========================================
    doc["wifi_ssid"] = wifi_ssid;
    doc["wifi_pass"] = wifi_pass;
    doc["language"] = language;
    doc["sleep_time_ms"] = sleep_time_ms;
    doc["decode_anim_style"] = decode_anim_style;

    doc["auto_push_enable"] = auto_push_enable;
    doc["auto_push_min_min"] = auto_push_min_min;
    doc["auto_push_max_min"] = auto_push_max_min;

    doc["pom_idx"] = pomodoro_current_idx;
    JsonArray pm_arr = doc["pom_presets"].to<JsonArray>();
    for (int i = 0; i < 5; i++) {
        JsonObject obj = pm_arr.add<JsonObject>();
        obj["n"] = pomodoro_presets[i].name;
        obj["w"] = pomodoro_presets[i].work_min;
        obj["r"] = pomodoro_presets[i].rest_min;
    }

    doc["alarm_count"] = alarm_count;
    JsonArray al_arr = doc["alarms"].to<JsonArray>();
    for (int i = 0; i < alarm_count; i++) {
        JsonObject obj = al_arr.add<JsonObject>();
        obj["en"] = alarms[i].is_active;
        obj["h"] = alarms[i].hour;
        obj["m"] = alarms[i].min;
        obj["n"] = alarms[i].name;
        obj["p"] = alarms[i].prescript;
    }

    doc["schedule_count"] = schedule_count;
    JsonArray sc_arr = doc["schedules"].to<JsonArray>();
    for (int i = 0; i < schedule_count; i++) {
        JsonObject obj = sc_arr.add<JsonObject>();
        obj["tt"] = schedules[i].target_time;
        obj["et"] = schedules[i].expire_time;
        obj["tl"] = schedules[i].title;
        obj["ps"] = schedules[i].prescript;
        obj["ex"] = schedules[i].is_expired;
        obj["rs"] = schedules[i].is_restored;
    }

    doc["cp_count"] = custom_prescript_count;
    JsonArray cp_arr = doc["custom_p"].to<JsonArray>();
    for (int i = 0; i < custom_prescript_count; i++) {
        cp_arr.add(custom_prescripts[i]);
    }

    // ==========================================
    // 序列化并写入硬盘
    // ==========================================
    String json_output;
    serializeJson(doc, json_output);
    SysFS_Write_File(CONFIG_FILE, json_output.c_str());
    
    Serial.println("[CONFIG] 系统协议已覆写至 /config.json");
}