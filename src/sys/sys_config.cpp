// 文件：src/sys/sys_config.cpp
#include "sys_config.h"
#include "sys_fs.h"      // 引入我们上一版的 LittleFS 中枢
#include <ArduinoJson.h> // 引入 JSON 引擎

SysConfig sysConfig;

const char *CONFIG_FILE = "/assets/config.json"; // 配置文件在硬盘上的绝对路径

void SysConfig::load()
{
    // 1. 从 LittleFS 硬盘读取 JSON 文本
    String json = SysFS_Read_File(CONFIG_FILE);

    // 2. 准备 JSON 解析引擎
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    // ==========================================
    // 如果读取失败、文件不存在、或者格式错误，赋予出厂默认值
    // ==========================================
    if (error || json == "")
    {
        Serial.println("[CONFIG] 配置文件无效或不存在，加载出厂默认协议...");

        wifi_ssid = "Your_WiFi_Name";
        wifi_pass = "12345678";
        language = 1;
        sleep_time_ms = 30000;
        true_sleep_time_ms = 0xFFFFFFFF;
        decode_anim_style = 0;
        auto_push_enable = false;
        auto_push_min_min = 30;
        auto_push_max_min = 120;
        coin_data.mode = 0;
        coin_data.sanity = 0;
        pomodoro_current_idx = 0;
        volume = 40; // 【新增】：默认音量设为 7
        const char *def_names[5] = {"常规专注", "深度工作", "短时冲刺", "阅读模式", "冥想休息"};
        uint32_t def_w[5] = {25, 60, 15, 45, 10};
        uint32_t def_r[5] = {5, 10, 3, 10, 5};
        for (int i = 0; i < 5; i++)
        {
            pomodoro_presets[i].name = def_names[i];
            pomodoro_presets[i].work_min = def_w[i];
            pomodoro_presets[i].rest_min = def_r[i];
        }

        alarm_count = 0;
        schedule_count = 0;
        haptic_enable = true;
        haptic_intensity = 3; // 默认拉满！
        nfc_mode = 0;
        gacha_stats.total = 0;
        gacha_stats.s3 = 0;
        gacha_stats.s2 = 0;
        gacha_stats.s1 = 0;
        gacha_stats.w3 = 0;
        gacha_stats.w2 = 0;

        // [大扫除]：删除了旧版 custom_prescript_count = 0; 的初始化

        // 生成默认文件并保存
        save();
        return;
    }

    // ==========================================
    // 从 JSON 树中精准解析数据
    // ==========================================
    wifi_ssid = doc["wifi_ssid"] | "Your_WiFi_Name";
    wifi_pass = doc["wifi_pass"] | "12345678";
    language = doc["language"] | 1;
    sleep_time_ms = doc["sleep_time_ms"] | 30000;
    true_sleep_time_ms = doc["true_sleep_time_ms"] | 0xFFFFFFFF;
    decode_anim_style = doc["decode_anim_style"] | 0;
    auto_push_enable = doc["auto_push_enable"] | false;
    auto_push_min_min = doc["auto_push_min_min"] | 30;
    auto_push_max_min = doc["auto_push_max_min"] | 120;
    volume = doc["volume"] | 40;
    if (volume > 100)
        volume = 100;
    pomodoro_current_idx = doc["pom_idx"] | 0;
    JsonArray pm_arr = doc["pom_presets"];
    for (int i = 0; i < 5; i++)
    {
        pomodoro_presets[i].name = pm_arr[i]["n"] | "预设";
        pomodoro_presets[i].work_min = pm_arr[i]["w"] | 25;
        pomodoro_presets[i].rest_min = pm_arr[i]["r"] | 5;
    }
    JsonObject coin_node = doc["coin_app"];
    if (!coin_node.isNull())
    {
        coin_data.mode = coin_node["mode"] | 0;
        coin_data.sanity = coin_node["sanity"] | 0;
        coin_data.coin_count = coin_node["coin_count"] | 1;
        coin_data.coin_type = coin_node["coin_type"] | 0; // 【新增解析】
    }
    else
    {
        coin_data.mode = 0;
        coin_data.sanity = 0;
        coin_data.coin_count = 1;
        coin_data.coin_type = 0; // 【新增】
    }

    // 【修改】：绝对防呆，把硬币上限从原来的 4 或 8 改成 9！
    if (coin_data.coin_count < 1 || coin_data.coin_count > 9)
        coin_data.coin_count = 1;

    // 【新增】：限制硬币型号只能是 0, 1, 2
    if (coin_data.coin_type < 0 || coin_data.coin_type > 2)
        coin_data.coin_type = 0;
    // (在 load 函数内部，读取 coin_data 后面的位置加上这段)

    // 【新增】：读取硬币技能预设
    coin_preset_count = 0;
    // 【核心修复】：使用 V7 版本的新语法，安全判断它是否存在且为一个数组
    if (doc["coin_presets"].is<JsonArray>())
    {
        JsonArray cp_arr = doc["coin_presets"].as<JsonArray>();
        for (JsonObject obj : cp_arr)
        {
            if (coin_preset_count >= 10)
                break;
            coin_presets[coin_preset_count].name = obj["n"].as<String>();
            coin_presets[coin_preset_count].base_power = obj["bp"] | 4;
            coin_presets[coin_preset_count].coin_power = obj["cp"] | 5;
            coin_presets[coin_preset_count].coin_count = obj["cc"] | 3;
            coin_presets[coin_preset_count].coin_colors = obj["cl"].as<String>();
            coin_preset_count++;
        }
    }

    alarm_count = doc["alarm_count"] | 0;
    JsonArray al_arr = doc["alarms"];
    for (int i = 0; i < alarm_count; i++)
    {
        alarms[i].is_active = al_arr[i]["en"] | false;
        alarms[i].hour = al_arr[i]["h"] | 8;
        alarms[i].min = al_arr[i]["m"] | 0;
        alarms[i].name = al_arr[i]["n"] | "闹钟";
        alarms[i].prescript = al_arr[i]["p"] | "唤醒指令";
    }

    schedule_count = doc["schedule_count"] | 0;
    JsonArray sc_arr = doc["schedules"];
    for (int i = 0; i < schedule_count; i++)
    {
        schedules[i].target_time = sc_arr[i]["tt"] | 0;
        schedules[i].expire_time = sc_arr[i]["et"] | 0;
        schedules[i].title = sc_arr[i]["tl"] | "待办";
        schedules[i].prescript = sc_arr[i]["ps"] | "";
        schedules[i].is_expired = sc_arr[i]["ex"] | false;
        schedules[i].is_restored = sc_arr[i]["rs"] | false;
        schedules[i].is_hidden = sc_arr[i]["hd"] | false; // 【新增】：读取隐藏属性
    }
    // load() 里面加：
    haptic_enable = doc["hap_en"] | true;
    haptic_intensity = doc["hap_in"] | 3;

    nfc_mode = doc["nfc_m"] | 0;

    JsonObject gs_node = doc["gacha_stats"];
    if (!gs_node.isNull())
    {
        gacha_stats.total = gs_node["total"] | 0;
        gacha_stats.s3 = gs_node["s3"] | 0;
        gacha_stats.s2 = gs_node["s2"] | 0;
        gacha_stats.s1 = gs_node["s1"] | 0;
        gacha_stats.w3 = gs_node["w3"] | 0;
        gacha_stats.w2 = gs_node["w2"] | 0;
    }
    else
    {
        gacha_stats.total = 0;
        gacha_stats.s3 = 0;
        gacha_stats.s2 = 0;
        gacha_stats.s1 = 0;
        gacha_stats.w3 = 0;
        gacha_stats.w2 = 0;
    }
}

void SysConfig::save()
{
    JsonDocument doc;

    // ==========================================
    // 将内存数据打包成 JSON 树
    // ==========================================
    doc["wifi_ssid"] = wifi_ssid;
    doc["wifi_pass"] = wifi_pass;
    doc["language"] = language;
    doc["sleep_time_ms"] = sleep_time_ms;
    doc["true_sleep_time_ms"] = true_sleep_time_ms;
    doc["decode_anim_style"] = decode_anim_style;

    doc["auto_push_enable"] = auto_push_enable;
    doc["auto_push_min_min"] = auto_push_min_min;
    doc["auto_push_max_min"] = auto_push_max_min;
    doc["volume"] = volume; // 【新增】：打包音量数据
    doc["pom_idx"] = pomodoro_current_idx;
    JsonArray pm_arr = doc["pom_presets"].to<JsonArray>();
    for (int i = 0; i < 5; i++)
    {
        JsonObject obj = pm_arr.add<JsonObject>();
        obj["n"] = pomodoro_presets[i].name;
        obj["w"] = pomodoro_presets[i].work_min;
        obj["r"] = pomodoro_presets[i].rest_min;
    }

    doc["alarm_count"] = alarm_count;
    JsonArray al_arr = doc["alarms"].to<JsonArray>();
    for (int i = 0; i < alarm_count; i++)
    {
        JsonObject obj = al_arr.add<JsonObject>();
        obj["en"] = alarms[i].is_active;
        obj["h"] = alarms[i].hour;
        obj["m"] = alarms[i].min;
        obj["n"] = alarms[i].name;
        obj["p"] = alarms[i].prescript;
    }

    doc["schedule_count"] = schedule_count;
    JsonArray sc_arr = doc["schedules"].to<JsonArray>();
    for (int i = 0; i < schedule_count; i++)
    {
        JsonObject obj = sc_arr.add<JsonObject>();
        obj["tt"] = schedules[i].target_time;
        obj["et"] = schedules[i].expire_time;
        obj["tl"] = schedules[i].title;
        obj["ps"] = schedules[i].prescript;
        obj["ex"] = schedules[i].is_expired;
        obj["rs"] = schedules[i].is_restored;
        obj["hd"] = schedules[i].is_hidden; // 【新增】：写入隐藏属性
    }
    JsonObject coin_node = doc["coin_app"].to<JsonObject>();
    coin_node["mode"] = coin_data.mode;
    coin_node["sanity"] = coin_data.sanity;
    coin_node["coin_count"] = coin_data.coin_count;
    coin_node["coin_type"] = coin_data.coin_type; // 【新增写入】
    // (在 save 函数内部，写入 coin_node 之后加上这段)

    // 【新增】：写入硬币技能预设
    doc["coin_preset_count"] = coin_preset_count;
    JsonArray cp_arr = doc["coin_presets"].to<JsonArray>();
    for (int i = 0; i < coin_preset_count; i++)
    {
        JsonObject obj = cp_arr.add<JsonObject>();
        obj["n"] = coin_presets[i].name;
        obj["bp"] = coin_presets[i].base_power;
        obj["cp"] = coin_presets[i].coin_power;
        obj["cc"] = coin_presets[i].coin_count;
        obj["cl"] = coin_presets[i].coin_colors;
    }
    JsonObject gs_node_out = doc["gacha_stats"].to<JsonObject>();
    gs_node_out["total"] = gacha_stats.total;
    gs_node_out["s3"] = gacha_stats.s3;
    gs_node_out["s2"] = gacha_stats.s2;
    gs_node_out["s1"] = gacha_stats.s1;
    gs_node_out["w3"] = gacha_stats.w3;
    gs_node_out["w2"] = gacha_stats.w2;
    // [大扫除]：彻底删除了所有跟 custom_p 写入硬盘相关的代码！
    // save() 里面加：
    doc["hap_en"] = haptic_enable;
    doc["hap_in"] = haptic_intensity;
    doc["nfc_m"] = nfc_mode;
    // ==========================================
    // 序列化并写入硬盘
    // ==========================================
    String json_output;
    serializeJson(doc, json_output);
    SysFS_Write_File(CONFIG_FILE, json_output.c_str());

    Serial.println("[CONFIG] 系统协议已覆写至 /config.json");
}