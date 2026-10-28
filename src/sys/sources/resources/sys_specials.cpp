/*
【模块职责】特殊指令实现。通过统一资源IO按当前语言读取FATFS素材，抽取时先推进人物链条概率，再抽纯特殊指令，并把进度写回sysConfig。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys/resources/sys_specials.h"
#include "sys/sys_config.h"
#include "sys/resources/sys_resource_io.h"
#include "sys/storage/sys_fs.h"
#include "sys/sys_psram_json.h"
#include "sys/sys_psram_text.h"
#include <ArduinoJson.h> // 必须引入 JSON 引擎
#include "lang/terminal_lang.h"

SysSpecials sysSpecials;
// --- PSRAM 数据结构定义 ---
struct PureSpecial
{
    SysPsramString id;
    uint16_t color;
    int prob;
    SysPsramString popup_title;
    SysPsramString text;
    SysPsramString audio_bind;
};

struct CharChain
{
    SysPsramString char_id;
    SysPsramString name;
    uint16_t color;
    int prob;
    SysPsramString popup_title;
    SysPsramTextList texts;
    SysPsramString audio_bind;
};

// 两层容器和全部字符串都优先位于PSRAM，语言切换时clear()统一释放旧目录。
SysPsramVector<PureSpecial> pool_pure_specials;
SysPsramVector<CharChain> pool_char_chains;

// 【函数说明】按当前语言读取 FATFS 中的 specials.json，解析人物链条和纯特殊指令。
void SysSpecials::begin(SystemLang_t lang)
{
    pool_pure_specials.clear();
    pool_char_chains.clear();

    current_lang_ = lang;
    const SystemLang_t current_lang = current_lang_;
    const SysResourcePath path = {TerminalLang::SpecialsPath(current_lang)};
    fs::File f;
    String resolvedPath;
    if (!SysResourceIO::OpenRead(path, f, "特殊指令", &resolvedPath))
        return;

    // 使用 ArduinoJson 7 的自动内存管理
    JsonDocument doc(SysPsramJsonAllocator::Instance());
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        Serial.printf("[特殊指令] JSON解析失败：%s，错误=%s。\n", resolvedPath.c_str(), err.c_str());
        return;
    }

    // 1. 吸入纯特殊指令 (如异想体)
    JsonArray pure_arr = doc["pure_specials"];
    for (JsonObject obj : pure_arr)
    {
        PureSpecial ps;
        ps.id = obj["id"] | "";
        ps.color = (uint16_t)strtol(obj["color"].as<const char *>(), NULL, 16);
        ps.prob = obj["prob"].as<int>();
        ps.popup_title = obj["popup_title"] | "";
        ps.text = obj["text"] | "";
        ps.audio_bind = obj["audio"] | ""; // 【新增】：如果没有配置，默认为空
        pool_pure_specials.push_back(ps);
    }

    // 2. 吸入人物专属链条
    JsonArray char_arr = doc["character_chains"];
    for (JsonObject obj : char_arr)
    {
        CharChain cc;
        cc.char_id = obj["char_id"] | "";
        cc.name = obj["name"] | "";
        cc.color = (uint16_t)strtol(obj["color"].as<const char *>(), NULL, 16);
        cc.prob = obj["prob"].as<int>();
        cc.popup_title = obj["popup_title"] | "";
        cc.audio_bind = obj["audio"] | ""; // 【新增】

        JsonArray texts_arr = obj["texts"];
        for (const char *txt : texts_arr)
        {
            cc.texts.emplace_back(txt);
        }
        pool_char_chains.push_back(cc);
    }

    Serial.printf("[特殊指令] 已预加载：特殊指令=%d，人物链条=%d，路径=%s。\n",
                  pool_pure_specials.size(), pool_char_chains.size(), resolvedPath.c_str());
}

// 【函数说明】指令抽取核心：先按人物链条概率尝试推进特殊剧情，再抽纯特殊指令，最后回落到普通指令池。
void SysSpecials::rollRandom()
{
    const SystemLang_t current_lang = current_lang_;

    // 默认兜底参数
    current_draw.color = 0x07FF;
    current_draw.is_special = false;
    current_draw.title = (current_lang == LANG_ZH) ? "【 接收指令 】" : "[ RECEIVE PRESCRIPT ]";

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
                current_draw.title = pool_char_chains[i].popup_title.c_str();
                current_draw.text = pool_char_chains[i].texts[sysConfig.char_progress[i]].c_str();
                current_draw.audio_bind = pool_char_chains[i].audio_bind.c_str();
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
            current_draw.title = pool_pure_specials[i].popup_title.c_str();
            current_draw.text = pool_pure_specials[i].text.c_str();
            current_draw.audio_bind = pool_pure_specials[i].audio_bind.c_str();
            return;
        }
    }

    // ==========================================
    // 兜底方案：啥也没拦截到，老老实实抽普通的
    // ==========================================
    current_draw.audio_bind = ""; // <--- 【清理缓存，防止幽灵音效】
    if (current_lang == LANG_ZH)
    {
        int sz = sys_prescripts_zh.size();
        current_draw.text = (sz > 0) ? sys_prescripts_zh[random(sz)].c_str() : "错误：中文指令库为空";
    }
    else
    {
        int sz = sys_prescripts_en.size();
        current_draw.text = (sz > 0) ? sys_prescripts_en[random(sz)].c_str() : "ERR: EN DB EMPTY";
    }
}

// 【函数说明】把外部文本直接设为当前抽取结果，用于 TXT、闹钟、日程这类指定内容。
void SysSpecials::setCustom(const char *custom_text, uint16_t font_color, const char *title)
{
    const SystemLang_t current_lang = current_lang_;
    current_draw.is_special = false;
    current_draw.color = font_color;
    // 每次都重建标题，默认提醒不能继承前一条自定义标题；外部标题按原文显示，不重复包括号。
    current_draw.title = (title && *title) ? title :
        ((current_lang == LANG_ZH) ? "【 接收指令 】" : "[ RECEIVE PRESCRIPT ]");
    current_draw.text = String(custom_text);

    // 外部 TXT / 闹钟 / 日程 / NFC 等自定义指令不是特殊指令，
    // 必须主动清空上一次特殊指令留下的音频绑定，避免出现“幽灵音效”。
    current_draw.audio_bind = "";
}
// 【函数说明】按 ID 查找特殊指令，锁定为当前抽取结果并更新人物链条进度。
void SysSpecials::forceDrawByID(const String &id)
{
    const SystemLang_t current_lang = current_lang_;

    // 1. 优先检索纯特殊指令 (ABN_01 等)
    for (auto &ps : pool_pure_specials)
    {
        if (ps.id == id)
        {
            current_draw.is_special = true;
            current_draw.color = ps.color;
            current_draw.title = ps.popup_title.c_str();
            current_draw.text = ps.text.c_str();
            current_draw.audio_bind = ps.audio_bind.c_str();
            return;
        }
    }

    // 2. 检索人物链条 (ishmael 等)
    for (int i = 0; i < pool_char_chains.size(); i++)
    {
        if (pool_char_chains[i].char_id == id)
        {
            // 【核心逻辑】：既然是强行触发，我们重置该人物进度到第 0 条
            sysConfig.char_progress[i] = 0;

            current_draw.is_special = true;
            current_draw.color = pool_char_chains[i].color;
            current_draw.title = pool_char_chains[i].popup_title.c_str();
            current_draw.text = pool_char_chains[i].texts[0].c_str(); // 强行触发第一条
            current_draw.audio_bind = pool_char_chains[i].audio_bind.c_str();
            // 推进进度到 1，这样后续通过 rollRandom 随机抽取的“意志共鸣”就能接上后面的剧情
            sysConfig.char_progress[i] = 1;
            sysConfig.save();
            Serial.printf("[特异点] 强行锁定人物链条: %s, 进度重置为 1\n", id.c_str());
            return;
        }
    }

    current_draw.is_special = true; // 依然认定为特殊打断，以便触发弹窗
    current_draw.color = 0xF800;    // 错误警报：刺眼的纯红色

    // 未找到 ID 的错误警报同样不能继承上一条特殊指令的音频。
    current_draw.audio_bind = "";

    if (current_lang == LANG_ZH)
    {
        current_draw.title = "【 指令检索失败 】";
        current_draw.text = "未找到对应条目：ID [" + id + "]";
    }
    else
    {
        current_draw.title = "[ PRESCRIPT NOT FOUND ]";
        current_draw.text = "No matching entry: ID [" + id + "]";
    }
}

// 【函数说明】按 ID 判断特殊指令是否存在（人物链条或纯特殊指令）。
bool SysSpecials::hasID(const String &id) const
{
    for (const auto &ps : pool_pure_specials)
        if (ps.id == id)
            return true;
    for (const auto &cc : pool_char_chains)
        if (cc.char_id == id)
            return true;
    return false;
}

/**
 * 【函数说明】按序号取一条特殊指令的元数据，供 SPC.LIST 逐条输出：先纯特殊指令，后人物链条。
 * 字段：kind（P 纯特殊 / C 人物链条）、id、name、prob、prog、total、color（0xRRRR 十六进制）、title、enabled。
 */
bool SysSpecials::metaAt(uint16_t index, JsonDocument &out) const
{
    char color[8];
    if (index < pool_pure_specials.size())
    {
        const auto &ps = pool_pure_specials[index];
        snprintf(color, sizeof(color), "0x%04X", ps.color);
        out["kind"] = "P";
        out["id"] = ps.id.c_str();
        out["name"] = ps.id.c_str();
        out["prob"] = ps.prob;
        out["prog"] = 1;
        out["total"] = 1;
        out["color"] = color;
        out["title"] = ps.popup_title.c_str();
        out["enabled"] = true;
        return true;
    }
    const size_t chain = index - pool_pure_specials.size();
    if (chain >= pool_char_chains.size())
        return false;
    const auto &cc = pool_char_chains[chain];
    snprintf(color, sizeof(color), "0x%04X", cc.color);
    out["kind"] = "C";
    out["id"] = cc.char_id.c_str();
    out["name"] = cc.name.c_str();
    out["prob"] = cc.prob;
    out["prog"] = sysConfig.char_progress[chain];
    out["total"] = static_cast<int>(cc.texts.size());
    out["color"] = color;
    out["title"] = cc.popup_title.c_str();
    out["enabled"] = (sysConfig.special_toggles & (1 << chain)) != 0;
    return true;
}

/**
 * 【函数说明】按 ID 取特殊指令正文：人物链条返回当前进度对应的下一条，链条已抽完时返回结案提示；
 * 纯特殊指令返回其固定正文。找不到 ID 时返回 false。
 */
bool SysSpecials::textByID(const String &id, String &out) const
{
    for (size_t i = 0; i < pool_char_chains.size(); i++)
    {
        if (!(pool_char_chains[i].char_id == id))
            continue;
        const int prog = sysConfig.char_progress[i];
        if (prog < static_cast<int>(pool_char_chains[i].texts.size()))
            out = pool_char_chains[i].texts[prog].c_str();
        else
            out = (current_lang_ == LANG_ZH) ? "[ 观测日志已完结，目标数据归档完毕 ]"
                                                        : "[ OBSERVATION LOG COMPLETE. TARGET DATA ARCHIVED. ]";
        return true;
    }
    for (const auto &ps : pool_pure_specials)
    {
        if (ps.id == id)
        {
            out = ps.text.c_str();
            return true;
        }
    }
    return false;
}
