#include "sys_specials.h"
#include "sys_config.h"
#include "app_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h> // 必须引入 JSON 引擎
#include <vector>

SysSpecials sysSpecials;

// --- PSRAM 数据结构定义 ---
struct PureSpecial
{
    String id;
    uint16_t color;
    int prob;
    String popup_title;
    String text;
};

struct CharChain
{
    String char_id;
    String name;
    uint16_t color;
    int prob;
    String popup_title;
    std::vector<String> texts;
};

// --- 常驻内存池 ---
std::vector<PureSpecial> pool_pure_specials;
std::vector<CharChain> pool_char_chains;

// 引用外部普通指令库
extern std::vector<String> sys_prescripts_zh;
extern std::vector<String> sys_prescripts_en;

void SysSpecials::begin()
{
    pool_pure_specials.clear();
    pool_char_chains.clear();

    SystemLang_t current_lang = appManager.getLanguage();
    String path = (current_lang == LANG_ZH) ? "/assets/specials_zh.json" : "/assets/specials_en.json";

    File f = LittleFS.open(path, "r");
    if (!f)
    {
        Serial.println("[SysSpecials] 严重警告：找不到特异点配置文件: " + path);
        return;
    }

    // 使用 ArduinoJson 7 的自动内存管理
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        Serial.printf("[SysSpecials] JSON 解析失败: %s\n", err.c_str());
        return;
    }

    // 1. 吸入纯特殊指令 (如异想体)
    JsonArray pure_arr = doc["pure_specials"];
    for (JsonObject obj : pure_arr)
    {
        PureSpecial ps;
        ps.id = obj["id"].as<String>();
        ps.color = (uint16_t)strtol(obj["color"].as<const char *>(), NULL, 16);
        ps.prob = obj["prob"].as<int>();
        ps.popup_title = obj["popup_title"].as<String>();
        ps.text = obj["text"].as<String>();
        pool_pure_specials.push_back(ps);
    }

    // 2. 吸入人物专属链条
    JsonArray char_arr = doc["character_chains"];
    for (JsonObject obj : char_arr)
    {
        CharChain cc;
        cc.char_id = obj["char_id"].as<String>();
        cc.name = obj["name"].as<String>();
        cc.color = (uint16_t)strtol(obj["color"].as<const char *>(), NULL, 16);
        cc.prob = obj["prob"].as<int>();
        cc.popup_title = obj["popup_title"].as<String>();

        JsonArray texts_arr = obj["texts"];
        for (const char *txt : texts_arr)
        {
            cc.texts.push_back(String(txt));
        }
        pool_char_chains.push_back(cc);
    }

    Serial.printf("[SysSpecials] 装载完成! 包含 %d 条特殊指令, %d 个人物链条。\n",
                  pool_pure_specials.size(), pool_char_chains.size());
}

void SysSpecials::rollRandom()
{
    SystemLang_t current_lang = appManager.getLanguage();

    // 默认兜底参数
    current_draw.color = 0x07FF;
    current_draw.is_special = false;
    current_draw.title = (current_lang == LANG_ZH) ? "【 接收都市意志 】" : "[ RECEIVE PRESCRIPT ]";

    int roll = random(10000); // 生成 0 ~ 9999 的万分位随机数
    int current_weight = 0;

    // ==========================================
    // 拦截器 1层：检查人物链条是否触发
    // ==========================================
    // ==========================================
    // 拦截器 1层：检查人物链条是否触发 (加入“意志共鸣”动态概率)
    // ==========================================
    for (int i = 0; i < pool_char_chains.size(); i++)
    {
        if (i >= 8)
            break;

        if ((sysConfig.special_toggles & (1 << i)) &&
            (sysConfig.char_progress[i] < pool_char_chains[i].texts.size()))
        {

            // 1. 获取基础概率 (例如 150 = 1.5%)
            int dynamic_prob = pool_char_chains[i].prob;

            // 2. 【核心机制：意志共鸣】
            // 如果进度大于 0，说明玩家已经卷入了该人物的命运链条。
            // 进度越深，抽中下一句的概率越高！
            // 这里设定为：每深入一步，额外增加 500 的权重（即绝对概率增加 5%）
            if (sysConfig.char_progress[i] > 0)
            {
                dynamic_prob += (sysConfig.char_progress[i] * 500);

                // 可选：如果你想让后面的剧情极其容易触发，可以用乘法倍率：
                // dynamic_prob = dynamic_prob * (sysConfig.char_progress[i] + 2);
            }

            current_weight += dynamic_prob;

            if (roll < current_weight)
            {
                // 【命中！】
                current_draw.is_special = true;
                current_draw.color = pool_char_chains[i].color;
                current_draw.title = pool_char_chains[i].popup_title;
                current_draw.text = pool_char_chains[i].texts[sysConfig.char_progress[i]];

                sysConfig.char_progress[i]++;
                sysConfig.save();
                return;
            }
        }
    }

    // ==========================================
    // 拦截器 2层：检查纯特殊指令是否触发
    // ==========================================
    for (int i = 0; i < pool_pure_specials.size(); i++)
    {
        current_weight += pool_pure_specials[i].prob;
        if (roll < current_weight)
        {
            // 【命中！】
            current_draw.is_special = true;
            current_draw.color = pool_pure_specials[i].color;
            current_draw.title = pool_pure_specials[i].popup_title;
            current_draw.text = pool_pure_specials[i].text;
            return;
        }
    }

    // ==========================================
    // 兜底方案：啥也没拦截到，老老实实抽普通的
    // ==========================================
    if (current_lang == LANG_ZH)
    {
        int sz = sys_prescripts_zh.size();
        current_draw.text = (sz > 0) ? sys_prescripts_zh[random(sz)] : "错误：中文指令库为空";
    }
    else
    {
        int sz = sys_prescripts_en.size();
        current_draw.text = (sz > 0) ? sys_prescripts_en[random(sz)] : "ERR: EN DB EMPTY";
    }
}

void SysSpecials::setCustom(const char *custom_text)
{
    SystemLang_t current_lang = appManager.getLanguage();
    current_draw.is_special = false;
    current_draw.color = 0x07FF;
    current_draw.title = (current_lang == LANG_ZH) ? "【 接受都市意志 】" : "[ OVERRIDE PRESCRIPT ]";
    current_draw.text = String(custom_text);
}