/*
【模块职责】特殊指令实现。按当前语言读取 specials_zh/en.json，抽取时先推进人物链条概率，再抽纯特殊指令，并把进度写回 sysConfig。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys_specials.h"
#include "sys_config.h"
#include "app_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h> // 必须引入 JSON 引擎
#include <vector>
#include "sys_ble.h" // 确保引入了蓝牙接口
#include "sys_event.h"
#include "../lang/terminal_lang.h"

SysSpecials sysSpecials;
// 【新增】：收到蓝牙同步请求时的回调函数
// 【函数说明】WebBLE 同步回调：发送特殊指令元数据，让网页显示人物链条、概率、进度、颜色和启用状态。
static void _Cb_Spc_Sync_Req(void *payload)
{
    // 收到广播，特异点引擎主动交出元数据
    sysSpecials.syncMetaData();
}
// --- PSRAM 数据结构定义 ---
struct PureSpecial
{
    String id;
    uint16_t color;
    int prob;
    String popup_title;
    String text;
    String audio_bind; // 【新增】
};

struct CharChain
{
    String char_id;
    String name;
    uint16_t color;
    int prob;
    String popup_title;
    std::vector<String> texts;
    String audio_bind; // 【新增】
};

// --- 常驻内存池 ---
std::vector<PureSpecial> pool_pure_specials;
std::vector<CharChain> pool_char_chains;

// 引用外部普通指令库
extern std::vector<String> sys_prescripts_zh;
extern std::vector<String> sys_prescripts_en;

// 【函数说明】按当前语言读取 specials_zh/en.json，解析人物链条和纯特殊指令，并订阅 BLE 同步事件。
void SysSpecials::begin()
{
    pool_pure_specials.clear();
    pool_char_chains.clear();

    SystemLang_t current_lang = appManager.getLanguage();
    String path = TerminalLang::SpecialsPath(current_lang);

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
        ps.audio_bind = obj["audio"] | ""; // 【新增】：如果没有配置，默认为空
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
        cc.audio_bind = obj["audio"] | ""; // 【新增】

        JsonArray texts_arr = obj["texts"];
        for (const char *txt : texts_arr)
        {
            cc.texts.push_back(String(txt));
        }
        pool_char_chains.push_back(cc);
    }

    Serial.printf("[SysSpecials] 装载完成! 包含 %d 条特殊指令, %d 个人物链条。\n",
                  pool_pure_specials.size(), pool_char_chains.size());
    // 【新增】：向邮局订阅蓝牙同步广播
    SysEvent_Subscribe(EVT_BLE_SYNC_REQ, _Cb_Spc_Sync_Req);
}

// 【函数说明】指令抽取核心：先按人物链条概率尝试推进特殊剧情，再抽纯特殊指令，最后回落到普通指令池。
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
                current_draw.audio_bind = pool_char_chains[i].audio_bind; // <--- 【补上这句】
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
            current_draw.audio_bind = pool_pure_specials[i].audio_bind; // <--- 【补上这句】
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
        current_draw.text = (sz > 0) ? sys_prescripts_zh[random(sz)] : "错误：中文指令库为空";
    }
    else
    {
        int sz = sys_prescripts_en.size();
        current_draw.text = (sz > 0) ? sys_prescripts_en[random(sz)] : "ERR: EN DB EMPTY";
    }
}

// 【函数说明】把外部文本直接设为当前抽取结果，用于 TXT、闹钟、日程这类指定内容。
void SysSpecials::setCustom(const char *custom_text)
{
    SystemLang_t current_lang = appManager.getLanguage();
    current_draw.is_special = false;
    current_draw.color = 0x07FF;
    current_draw.title = (current_lang == LANG_ZH) ? "【 接受都市意志 】" : "[ OVERRIDE PRESCRIPT ]";
    current_draw.text = String(custom_text);

    // 外部 TXT / 闹钟 / 日程 / NFC 等自定义指令不是特殊指令，
    // 必须主动清空上一次特殊指令留下的音频绑定，避免出现“幽灵音效”。
    current_draw.audio_bind = "";
}
// 【函数说明】按 ID 查找特殊指令，锁定为当前抽取结果并更新人物链条进度。
void SysSpecials::forceDrawByID(const String &id)
{
    SystemLang_t current_lang = appManager.getLanguage();

    // 1. 优先检索纯特殊指令 (ABN_01 等)
    for (auto &ps : pool_pure_specials)
    {
        if (ps.id == id)
        {
            current_draw.is_special = true;
            current_draw.color = ps.color;
            current_draw.title = ps.popup_title;
            current_draw.text = ps.text;
            current_draw.audio_bind = ps.audio_bind;
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
            current_draw.title = pool_char_chains[i].popup_title;
            current_draw.text = pool_char_chains[i].texts[0]; // 强行触发第一条
            current_draw.audio_bind = pool_char_chains[i].audio_bind;
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
        current_draw.title = "【 警告：未登记的特异点 】";
        current_draw.text = "请求被驳回：无法解析目标 ID [" + id + "]";
    }
    else
    {
        current_draw.title = "[ WARN: UNREGISTERED SPC ]";
        current_draw.text = "Request Denied: Unresolvable Target ID [" + id + "]";
    }
}

// 【函数说明】把所有特殊指令的 ID、名称、概率、进度、颜色和启用状态通过 BLE 发给网页。
void SysSpecials::syncMetaData()
{
    // 1. 同步纯指令 (异想体等)
    for (int i = 0; i < pool_pure_specials.size(); i++)
    {
        auto &ps = pool_pure_specials[i];
        char buf[256];
        // 协议格式：SPC_META:P|ID|名称|概率|进度|颜色HEX|弹窗标题|启用状态
        snprintf(buf, sizeof(buf), "SPC_META:P|%s|%s|%d|1/1|0x%04X|%s|1",
                 ps.id.c_str(), ps.id.c_str(), ps.prob, ps.color, ps.popup_title.c_str());
        SysBLE_Notify(buf);
        delay(20); // 必须加微小延时，防止 BLE 队列撑爆
    }

    // 2. 同步人物剧本链条
    for (int i = 0; i < pool_char_chains.size(); i++)
    {
        auto &cc = pool_char_chains[i];
        int prog = sysConfig.char_progress[i];
        int total = cc.texts.size();
        int enabled = (sysConfig.special_toggles & (1 << i)) ? 1 : 0;
        char buf[256];
        snprintf(buf, sizeof(buf), "SPC_META:C|%s|%s|%d|%d/%d|0x%04X|%s|%d",
                 cc.char_id.c_str(), cc.name.c_str(), cc.prob, prog, total, cc.color, cc.popup_title.c_str(), enabled);
        SysBLE_Notify(buf);
        delay(20);
    }
}

// 【函数说明】按 ID 查找特殊指令正文并通过 SPC_TXT 回传网页。
void SysSpecials::syncTextByID(const String &id)
{
    // 1. 在人物链条中寻找
    for (int i = 0; i < pool_char_chains.size(); i++)
    {
        if (pool_char_chains[i].char_id == id)
        {
            int prog = sysConfig.char_progress[i];
            String target_text;
            if (prog < pool_char_chains[i].texts.size())
            {
                target_text = pool_char_chains[i].texts[prog]; // 返回当前进度对应的下一条文本
            }
            else
            {
                target_text = (appManager.getLanguage() == LANG_ZH) ? "[ 观测日志已完结，目标数据归档完毕 ]" : "[ OBSERVATION LOG COMPLETE. TARGET DATA ARCHIVED. ]"; // 进度抽完的兜底
            }
            // 协议格式：SPC_TXT:ID|具体文本
            String msg = "SPC_TXT:" + id + "|" + target_text;
            SysBLE_Notify(msg.c_str());
            return;
        }
    }

    // 2. 在纯指令中寻找
    for (auto &ps : pool_pure_specials)
    {
        if (ps.id == id)
        {
            String msg = "SPC_TXT:" + id + "|" + ps.text;
            SysBLE_Notify(msg.c_str());
            return;
        }
    }
}
