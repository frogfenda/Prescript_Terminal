/*
【模块职责】纺织机回复池实现。

设计约束：
- 只维护答案池，不直接切页面、不播放音效、不调用 Prescript/Specials；
- AppOracle 抽到文本后，直接交给 ui_prescript_decoder 按普通指令动画显示；
- JSON 字段使用 id/type/weight/text，其中 type 用于区分“纺织机回复”和“吃什么”。
- JSON 原始素材由资源管家 sys_res 从 LittleFS 挂载到 PSRAM，本模块只解析已挂载的内存素材。
*/
#include "sys_oracle.h"
#include "app_manager.h"
#include "sys_res.h"
#include <ArduinoJson.h>
#include <vector>

SysOracle sysOracle;

namespace {

struct OracleEntry
{
    String id;
    String type;
    String text;
    int weight;

    // ESP32 当前工具链的 C++11 模式下，带默认成员初始化的结构体
    // 不能稳定接受 vector.push_back({ ... }) 这种聚合初始化。
    // 显式构造函数用于兼容 xtensa-esp32s3-elf-g++ 8.4.0。
    OracleEntry() : id(), type(), text(), weight(10) {}

    OracleEntry(const char* entry_id, const char* entry_type, const char* entry_text, int entry_weight)
        : id(entry_id), type(entry_type), text(entry_text), weight(entry_weight)
    {
    }
};

std::vector<OracleEntry> g_oracle_pool;
SystemLang_t g_loaded_lang = LANG_ZH;
bool g_loaded = false;
String g_last_weaver_id;
String g_last_food_id;

const char* oraclePath(SystemLang_t lang)
{
    return lang == LANG_ZH ? "/assets/oracle_zh.json" : "/assets/oracle_en.json";
}

const char* oracleJsonData(SystemLang_t lang)
{
    return lang == LANG_ZH ? g_oracle_json_zh : g_oracle_json_en;
}

uint32_t oracleJsonLen(SystemLang_t lang)
{
    return lang == LANG_ZH ? g_oracle_json_zh_len : g_oracle_json_en_len;
}

void addFallback(SystemLang_t lang)
{
    g_oracle_pool.clear();

    if (lang == LANG_ZH)
    {
        g_oracle_pool.push_back(OracleEntry("W_FALLBACK_01", "weaver", "纺织机没有找到答案，但它记住了你的问题。", 10));
        g_oracle_pool.push_back(OracleEntry("W_FALLBACK_02", "weaver", "你正在寻找的答案，已经被写进下一条指令。", 10));
        g_oracle_pool.push_back(OracleEntry("F_FALLBACK_01", "food", "吃面。不要问为什么。", 10));
        g_oracle_pool.push_back(OracleEntry("F_FALLBACK_02", "food", "今天适合吃热的东西。", 10));
    }
    else
    {
        g_oracle_pool.push_back(OracleEntry("W_FALLBACK_01", "weaver", "The loom found no answer, but it remembered your question.", 10));
        g_oracle_pool.push_back(OracleEntry("W_FALLBACK_02", "weaver", "The answer you seek has been written into the next prescript.", 10));
        g_oracle_pool.push_back(OracleEntry("F_FALLBACK_01", "food", "Eat noodles. Do not ask why.", 10));
        g_oracle_pool.push_back(OracleEntry("F_FALLBACK_02", "food", "Eat something warm today.", 10));
    }
}

bool loadOracleJson(SystemLang_t lang)
{
    g_oracle_pool.clear();

    const char* data = oracleJsonData(lang);
    uint32_t len = oracleJsonLen(lang);
    if (!data || len == 0)
    {
        Serial.printf("[纺织机] 资源管家未挂载答案池：%s，使用内置兜底。\n", oraclePath(lang));
        addFallback(lang);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);

    if (err)
    {
        Serial.printf("[纺织机] JSON 解析失败：%s，使用内置兜底。\n", err.c_str());
        addFallback(lang);
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr)
    {
        OracleEntry e;
        e.id = obj["id"].as<String>();
        e.type = obj["type"].as<String>();
        e.text = obj["text"].as<String>();
        e.weight = obj["weight"] | 10;

        e.id.trim();
        e.type.trim();
        e.text.trim();
        if (e.id.length() == 0 || e.type.length() == 0 || e.text.length() == 0)
            continue;
        if (e.weight <= 0)
            e.weight = 1;

        g_oracle_pool.push_back(e);
    }

    if (g_oracle_pool.empty())
    {
        Serial.printf("[纺织机] 答案池为空：%s，使用内置兜底。\n", oraclePath(lang));
        addFallback(lang);
        return false;
    }

    Serial.printf("[纺织机] 已解析 %s，答案 %d 条。\n", oraclePath(lang), (int)g_oracle_pool.size());
    return true;
}

String& lastIdForType(const char* type)
{
    if (type && strcmp(type, "food") == 0)
        return g_last_food_id;
    return g_last_weaver_id;
}

} // namespace

void SysOracle::begin()
{
    g_loaded_lang = appManager.getLanguage();
    loadOracleJson(g_loaded_lang);
    g_loaded = true;
}

bool SysOracle::ensureLoaded(SystemLang_t lang)
{
    if (!g_loaded || g_loaded_lang != lang)
    {
        g_loaded_lang = lang;
        loadOracleJson(lang);
        g_loaded = true;
    }
    return !g_oracle_pool.empty();
}

bool SysOracle::drawByType(const char* type, SystemLang_t lang, OracleAnswer& out)
{
    if (!type || !ensureLoaded(lang))
        return false;

    int total = 0;
    int match_count = 0;
    String& last_id = lastIdForType(type);

    for (const auto& e : g_oracle_pool)
    {
        if (e.type != type)
            continue;
        // 同一类型有多条时，尽量避免连续抽到完全同一条。
        if (last_id.length() > 0 && e.id == last_id)
            continue;
        total += max(1, e.weight);
        match_count++;
    }

    // 如果排除上一条后没有候选，就允许重复，避免答案池很小时抽不到。
    bool allow_repeat = false;
    if (total <= 0)
    {
        allow_repeat = true;
        for (const auto& e : g_oracle_pool)
        {
            if (e.type != type)
                continue;
            total += max(1, e.weight);
            match_count++;
        }
    }

    if (total <= 0 || match_count <= 0)
    {
        out.id = "LOOM_EMPTY";
        out.type = type;
        out.text = (lang == LANG_ZH) ? "纺织机没有给出回应。" : "The loom gives no answer.";
        return false;
    }

    int roll = random(total);
    int acc = 0;
    for (const auto& e : g_oracle_pool)
    {
        if (e.type != type)
            continue;
        if (!allow_repeat && last_id.length() > 0 && e.id == last_id)
            continue;

        acc += max(1, e.weight);
        if (roll < acc)
        {
            out.id = e.id;
            out.type = e.type;
            out.text = e.text;
            last_id = e.id;
            return true;
        }
    }

    return false;
}
