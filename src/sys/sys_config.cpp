/*
【模块职责】配置持久化实现。把公共配置和语言配置分别序列化到 LittleFS，启动时读取实体默认配置并限制数组计数避免损坏配置导致越界。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_config.cpp
#include "sys_config.h"
#include "sys_fs.h"      // 引入我们上一版的 LittleFS 中枢
#include <ArduinoJson.h> // 引入 JSON 引擎

SysConfig sysConfig;

// 配置文件路径在 sys_constants.h / terminal_lang.h 中统一定义。

namespace {

// 【函数说明】把 src JSON 对象的字段覆盖到 dst；用于把旧配置、公共配置和语言配置合成一次读取视图。
void MergeJsonObject(JsonDocument &dst, JsonDocument &src)
{
    JsonObject obj = src.as<JsonObject>();
    for (JsonPair kv : obj)
        dst[kv.key()] = kv.value();
}

bool ParseJsonIfPresent(const String &json, JsonDocument &doc)
{
    if (json.length() == 0)
        return false;
    DeserializationError err = deserializeJson(doc, json);
    return !err;
}

SystemLang_t DetectProfileLanguage(JsonDocument *common_doc, JsonDocument *legacy_doc)
{
    if (TerminalLang::LOCKED)
        return TerminalLang::DEFAULT_LANG;

    if (common_doc)
        return TerminalLang::Normalize((*common_doc)["language"] | (uint8_t)TerminalLang::DEFAULT_LANG);
    if (legacy_doc)
        return TerminalLang::Normalize((*legacy_doc)["language"] | (uint8_t)TerminalLang::DEFAULT_LANG);
    return TerminalLang::DEFAULT_LANG;
}

// 【函数说明】只有运行时配置、实体默认配置和旧配置全部失效时才使用这组急救默认。
// 正常出厂默认值应写在 data/common/config_common.json 与 data/zh|en/config_*.json 里。
void ApplyEmergencyDefaults(SysConfig &cfg)
{
    cfg.wifi_ssid = "Your_WiFi_Name";
    cfg.wifi_pass = "12345678";
    cfg.language = (uint8_t)TerminalLang::DEFAULT_LANG;
    cfg.sleep_time_ms = PrescriptConst::DEFAULT_IDLE_SLEEP_MS;
    cfg.true_sleep_time_ms = PrescriptConst::NEVER_SLEEP_MS;
    cfg.decode_anim_style = 0;
    cfg.auto_push_enable = false;
    cfg.auto_push_min_min = 30;
    cfg.auto_push_max_min = 120;
    cfg.time_auto_resync = true;
    cfg.time_resync_interval_min = 15;
    cfg.time_saved_epoch_valid = false;
    cfg.time_saved_epoch_utc = 0;
    cfg.coin_data.mode = 0;
    cfg.coin_data.sanity = 0;
    cfg.coin_data.coin_count = 1;
    cfg.coin_data.coin_type = 0;
    cfg.pomodoro_current_idx = 0;
    cfg.volume = 40;
    cfg.special_toggles = 0xFFFFFFFF;
    cfg.coin_preset_count = 0;
    cfg.prescript_target_count = 0;
    cfg.current_prescript_target = "";
    cfg.alarm_count = 0;
    cfg.schedule_count = 0;
    cfg.haptic_enable = true;
    cfg.haptic_intensity = 3;
    cfg.nfc_mode = 0;
    cfg.gacha_stats.total = 0;
    cfg.gacha_stats.s3 = 0;
    cfg.gacha_stats.s2 = 0;
    cfg.gacha_stats.s1 = 0;
    cfg.gacha_stats.w3 = 0;
    cfg.gacha_stats.w2 = 0;

    const char *fallback_names[PrescriptConst::MAX_POMODORO_PRESETS] = {
        "常规专注", "深度工作", "短时冲刺", "阅读模式", "冥想休息"
    };
    const uint32_t fallback_work[PrescriptConst::MAX_POMODORO_PRESETS] = {25, 60, 15, 45, 10};
    const uint32_t fallback_rest[PrescriptConst::MAX_POMODORO_PRESETS] = {5, 10, 3, 10, 5};
    for (int i = 0; i < PrescriptConst::MAX_POMODORO_PRESETS; i++)
    {
        cfg.pomodoro_presets[i].name = fallback_names[i];
        cfg.pomodoro_presets[i].work_min = fallback_work[i];
        cfg.pomodoro_presets[i].rest_min = fallback_rest[i];
    }
    for (int i = 0; i < PrescriptConst::MAX_CHAR_CHAINS; i++)
        cfg.char_progress[i] = 0;
}

} // namespace

// 【函数说明】读取 common/profile 双层配置；缺失时先用实体默认配置，最后才使用代码内急救默认。
void SysConfig::load()
{
    JsonDocument doc;
    JsonDocument legacy_doc;
    JsonDocument common_doc;
    JsonDocument common_default_doc;
    JsonDocument profile_doc;
    JsonDocument profile_default_doc;

    String legacy_json = SysFS_Read_File(PrescriptConst::CONFIG_LEGACY_FILE);
    String common_json = SysFS_Read_File(PrescriptConst::CONFIG_COMMON_FILE);
    String common_default_json = SysFS_Read_File(PrescriptConst::CONFIG_COMMON_DEFAULT_FILE);

    bool has_legacy = ParseJsonIfPresent(legacy_json, legacy_doc);
    bool has_common = ParseJsonIfPresent(common_json, common_doc);
    bool has_common_default = ParseJsonIfPresent(common_default_json, common_default_doc);
    SystemLang_t profile_lang = DetectProfileLanguage(
        has_common ? &common_doc : (has_common_default ? &common_default_doc : nullptr),
        has_legacy ? &legacy_doc : nullptr
    );
    String profile_json = SysFS_Read_File(TerminalLang::ConfigPath(profile_lang));
    String profile_default_json = SysFS_Read_File(TerminalLang::DefaultConfigPath(profile_lang));
    bool has_profile = ParseJsonIfPresent(profile_json, profile_doc);
    bool has_profile_default = ParseJsonIfPresent(profile_default_json, profile_default_doc);

    bool need_migrate_save = false;
    if (has_legacy)
    {
        MergeJsonObject(doc, legacy_doc);
        need_migrate_save = !has_common || !has_profile;
    }
    if (has_common_default)
        MergeJsonObject(doc, common_default_doc);
    if (has_profile_default)
        MergeJsonObject(doc, profile_default_doc);
    if (has_common)
        MergeJsonObject(doc, common_doc);
    if (has_profile)
        MergeJsonObject(doc, profile_doc);
    if (!has_common || !has_profile)
        need_migrate_save = true;

    // ==========================================
    // 如果运行时配置、实体默认配置和旧配置都无效，使用急救默认值让设备至少能启动。
    // ==========================================
    if (!has_legacy && !has_common && !has_profile && !has_common_default && !has_profile_default)
    {
        Serial.println("[CONFIG] 运行时配置和实体默认配置都无效，启用急救默认值。");
        ApplyEmergencyDefaults(*this);
        save();
        return;
    }

    // ==========================================
    // 从 JSON 树中精准解析数据
    // ==========================================
    wifi_ssid = doc["wifi_ssid"] | "Your_WiFi_Name";
    wifi_pass = doc["wifi_pass"] | "12345678";
    language = (uint8_t)TerminalLang::Normalize(doc["language"] | (uint8_t)TerminalLang::DEFAULT_LANG);
    if (TerminalLang::LOCKED)
        language = (uint8_t)TerminalLang::DEFAULT_LANG;
    sleep_time_ms = doc["sleep_time_ms"] | PrescriptConst::DEFAULT_IDLE_SLEEP_MS;
    true_sleep_time_ms = doc["true_sleep_time_ms"] | PrescriptConst::NEVER_SLEEP_MS;
    decode_anim_style = doc["decode_anim_style"] | 0;
    auto_push_enable = doc["auto_push_enable"] | false;
    auto_push_min_min = doc["auto_push_min_min"] | 30;
    auto_push_max_min = doc["auto_push_max_min"] | 120;
    // 时间系统策略。
    // 旧配置文件没有这些字段时，默认开启周期校时，间隔 15 分钟。
    time_auto_resync = doc["time_auto_resync"] | true;
    time_resync_interval_min = doc["time_resync_interval_min"] | 15;
    time_saved_epoch_valid = doc["time_saved_epoch_valid"] | false;
    time_saved_epoch_utc = doc["time_saved_epoch_utc"] | 0;

    // 防止公共配置被手动改坏后出现过短或过长的校时间隔。
    if (time_resync_interval_min < 5) time_resync_interval_min = 5;
    if (time_resync_interval_min > 240) time_resync_interval_min = 240;

    // 保存的网络时间只作为开机兜底；如果配置被手动改坏，直接失效。
    if (time_saved_epoch_utc < 1577836800UL || time_saved_epoch_utc > 2082758399UL)
    {
        time_saved_epoch_valid = false;
        time_saved_epoch_utc = 0;
    }
    volume = doc["volume"] | 40;
    if (volume > 100)
        volume = 100;
    pomodoro_current_idx = doc["pom_idx"] | 0;
    JsonArray pm_arr = doc["pom_presets"];
    for (int i = 0; i < PrescriptConst::MAX_POMODORO_PRESETS; i++)
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

    // 【边界保护】硬币数量由系统常量统一控制，避免设置页、协议和动画页各自写死上限。
    if (coin_data.coin_count < 1 || coin_data.coin_count > PrescriptConst::MAX_COIN_COUNT)
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
            if (coin_preset_count >= PrescriptConst::MAX_COIN_PRESETS)
                break;
            coin_presets[coin_preset_count].name = obj["n"].as<String>();
            coin_presets[coin_preset_count].base_power = obj["bp"] | 4;
            coin_presets[coin_preset_count].coin_power = obj["cp"] | 5;
            coin_presets[coin_preset_count].coin_count = obj["cc"] | 3;
            if (coin_presets[coin_preset_count].coin_count < 1 ||
                coin_presets[coin_preset_count].coin_count > PrescriptConst::MAX_COIN_COUNT)
                coin_presets[coin_preset_count].coin_count = 3;
            coin_presets[coin_preset_count].coin_colors = obj["cl"].as<String>();
            coin_preset_count++;
        }
    }

    // 【新增】：读取本地使用者 ID。
    // 这些 ID 只用于把“致...”改写成本地显示称呼，不关联网络账号或远程身份。
    prescript_target_count = 0;
    current_prescript_target = doc["current_prescript_target"] | "";
    current_prescript_target.trim();
    if (doc["prescript_targets"].is<JsonArray>())
    {
        JsonArray target_arr = doc["prescript_targets"].as<JsonArray>();
        for (JsonVariant value : target_arr)
        {
            if (prescript_target_count >= PrescriptConst::MAX_PRESCRIPT_TARGETS)
                break;

            String id = value.as<String>();
            id.trim();
            if (id.length() == 0)
                continue;
            if (id.length() > PrescriptConst::MAX_PRESCRIPT_TARGET_LEN)
                id = id.substring(0, PrescriptConst::MAX_PRESCRIPT_TARGET_LEN);

            prescript_targets[prescript_target_count++] = id;
        }
    }

    bool current_found = current_prescript_target.length() == 0;
    for (int i = 0; i < prescript_target_count; i++)
    {
        if (prescript_targets[i] == current_prescript_target)
        {
            current_found = true;
            break;
        }
    }
    if (!current_found)
        current_prescript_target = "";

    alarm_count = doc["alarm_count"] | 0;
    if (alarm_count > PrescriptConst::MAX_ALARMS) alarm_count = PrescriptConst::MAX_ALARMS;
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
    if (schedule_count > PrescriptConst::MAX_SCHEDULES) schedule_count = PrescriptConst::MAX_SCHEDULES;
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
    special_toggles = doc["spec_tog"] | 0xFFFFFFFF;
    
    for (int i = 0; i < PrescriptConst::MAX_CHAR_CHAINS; i++) char_progress[i] = 0;
    if (doc["char_prog"].is<JsonArray>())
    {
        JsonArray prog_arr = doc["char_prog"].as<JsonArray>();
        for (int i = 0; i < PrescriptConst::MAX_CHAR_CHAINS && i < prog_arr.size(); i++)
        {
            char_progress[i] = prog_arr[i] | 0;
        }
    }
    else
    {
        // 防呆保护：如果硬盘里没有这个数组，强制清零
        for (int i = 0; i < PrescriptConst::MAX_CHAR_CHAINS; i++) char_progress[i] = 0;
    }

    if (need_migrate_save)
    {
        Serial.println("[CONFIG] 检测到旧版配置，正在迁移为 common/profile 双层配置。");
        save();
    }
}

// 【函数说明】把当前 sysConfig 序列化成 JSON 写回 LittleFS，包含 WiFi、语言、音量、震动、日程、闹钟、硬币等配置。
void SysConfig::save()
{
    saveCommon();
    saveLanguageProfile(TerminalLang::Normalize(language));
}

void SysConfig::saveCommon()
{
    JsonDocument doc;

    // ==========================================
    // 将设备级公共数据打包成 JSON 树。
    // 这里不写闹钟、日程、使用者和特异点进度，避免中英文 profile 互相污染。
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
    // 保存周期校时策略和最近一次网络对时的 UTC epoch。
    // 保存的 epoch 只用于下次开机兜底显示，不代表断电期间真实时间流逝。
    doc["time_auto_resync"] = time_auto_resync;
    doc["time_resync_interval_min"] = time_resync_interval_min;
    doc["time_saved_epoch_valid"] = time_saved_epoch_valid;
    doc["time_saved_epoch_utc"] = time_saved_epoch_utc;
    doc["volume"] = volume; // 【新增】：打包音量数据

    JsonObject coin_node = doc["coin_app"].to<JsonObject>();
    coin_node["mode"] = coin_data.mode;
    coin_node["sanity"] = coin_data.sanity;
    coin_node["coin_count"] = coin_data.coin_count;
    coin_node["coin_type"] = coin_data.coin_type; // 【新增写入】

    JsonObject gs_node_out = doc["gacha_stats"].to<JsonObject>();
    gs_node_out["total"] = gacha_stats.total;
    gs_node_out["s3"] = gacha_stats.s3;
    gs_node_out["s2"] = gacha_stats.s2;
    gs_node_out["s1"] = gacha_stats.s1;
    gs_node_out["w3"] = gacha_stats.w3;
    gs_node_out["w2"] = gacha_stats.w2;

    doc["hap_en"] = haptic_enable;
    doc["hap_in"] = haptic_intensity;
    doc["nfc_m"] = nfc_mode;

    String json_output;
    serializeJson(doc, json_output);
    SysFS_Write_File(PrescriptConst::CONFIG_COMMON_FILE, json_output.c_str());

    Serial.println("[CONFIG] 公共配置已覆写至 /common/config.json");
}

void SysConfig::loadLanguageProfile(SystemLang_t lang)
{
    SystemLang_t old_lang = TerminalLang::Normalize(language);
    saveLanguageProfile(old_lang);
    language = (uint8_t)lang;
    saveCommon();
    load();
}

void SysConfig::saveLanguageProfile(SystemLang_t lang)
{
    JsonDocument doc;

    // 语言 profile 保存带文本语境或世界观进度的内容，使用者 ID 也按语言隔离。
    doc["pom_idx"] = pomodoro_current_idx;
    JsonArray pm_arr = doc["pom_presets"].to<JsonArray>();
    for (int i = 0; i < PrescriptConst::MAX_POMODORO_PRESETS; i++)
    {
        JsonObject obj = pm_arr.add<JsonObject>();
        obj["n"] = pomodoro_presets[i].name;
        obj["w"] = pomodoro_presets[i].work_min;
        obj["r"] = pomodoro_presets[i].rest_min;
    }

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

    // 【新增】：写入本地使用者 ID。
    // 当前使用者按语言单独保存，列表为空或当前使用者被删除时允许保存为空字符串。
    doc["current_prescript_target"] = current_prescript_target;
    JsonArray target_arr = doc["prescript_targets"].to<JsonArray>();
    for (int i = 0; i < prescript_target_count; i++)
    {
        target_arr.add(prescript_targets[i]);
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
    
    // 【新增】：保存特异点引擎数据
    doc["spec_tog"] = special_toggles;
    JsonArray prog_arr = doc["char_prog"].to<JsonArray>();
    for (int i = 0; i < PrescriptConst::MAX_CHAR_CHAINS; i++)
    {
        prog_arr.add(char_progress[i]);
    }

    // ==========================================
    // 序列化并写入硬盘
    // ==========================================
    String json_output;
    serializeJson(doc, json_output);
    SysFS_Write_File(TerminalLang::ConfigPath(lang), json_output.c_str());

    Serial.printf("[CONFIG] %s 语言配置已覆写至 %s\n", TerminalLang::Code(lang), TerminalLang::ConfigPath(lang));
}
