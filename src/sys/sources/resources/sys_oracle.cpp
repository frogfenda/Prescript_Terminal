/*
【模块职责】独立拥有纺织机库正文、分类与权重，通过本库内容UUID抽取；不改特殊链条进度。
【状态所有权】每个语言/分类保存上次内容UUID，运行期间避免连续重复，重启后重新开始。
【资源约束】内容与规则同文件提交；只兼容上一版UUID引用库迁移，损坏数据保留并使用内置提示。
*/
#include "sys/resources/sys_oracle.h"
#include "sys/sys_psram_json.h"
#include "hal/hal_fat_storage.h"
#include <algorithm>
SysOracle sysOracle;
namespace {
struct Entry { PrescriptId content; SysPsramString type; uint32_t weight=1; };
struct Rules { PrescriptLibrary library; SysPsramVector<Entry> entries; };
struct Last { SysPsramString type; PrescriptId content; };
Rules s_rules;
SysPsramVector<Last> s_last[2];
bool s_loaded=false;
bool s_valid=false;
bool UpgradeLegacy(JsonDocument &doc, SystemLang_t lang) {
    if (!doc["schema_version"].is<unsigned>() || doc["schema_version"].as<unsigned>() != 1 ||
        !doc["revision"].is<uint32_t>() || strcmp(doc["lang"] | "", TerminalLang::Code(lang)) ||
        !doc["entries"].is<JsonArray>()) return false;
    JsonDocument references(SysPsramJsonAllocator::Instance()); JsonArray ids = references.to<JsonArray>();
    for (JsonObjectConst entry : doc["entries"].as<JsonArrayConst>()) ids.add(entry["content_id"]);
    return !references.overflowed() && SysPrescript::MigrateLibrary(lang, "oracle", ids, doc);
}
bool Parse(const JsonDocument &doc,SystemLang_t lang,Rules &out) {
    if (!doc["entries"].is<JsonArrayConst>()) return false;
    Rules next; if (!SysPrescript::DecodeLibrary(doc,lang,"oracle",next.library)) return false;
    for (JsonObjectConst o : doc["entries"].as<JsonArrayConst>()) {
        Entry e;
        if (!e.content.parse(o["content_id"]|"") || !SysPrescript::Find(next.library,e.content) ||
            !o["type"].is<const char *>() || !e.type.assign(o["type"].as<const char *>()) || e.type.isEmpty() ||
            !o["weight"].is<uint32_t>() || !o["weight"].as<uint32_t>() || o["weight"].as<uint32_t>()>INT32_MAX) return false;
        e.weight=o["weight"]; uint64_t sum=e.weight;
        for (const auto &previous : next.entries) if (previous.type==e.type) {
            if (previous.content==e.content) return false;
            sum+=previous.weight;
        }
        if (sum>INT32_MAX) return false;
        next.entries.push_back(std::move(e));
    }
    out=std::move(next); return true;
}
PrescriptId &LastId(SystemLang_t lang,const char *type) {
    auto &states=s_last[lang==LANG_ZH ? 0 : 1];
    for (auto &s : states) if (s.type==type) return s.content;
    Last state; state.type=type; states.push_back(std::move(state)); return states.back().content;
}
bool Fallback(SystemLang_t lang,const char *type,OracleAnswer &out) {
    const bool food=!strcmp(type,"food");
    const char *messages[2];
    if (lang == LANG_ZH) {
        messages[0] = food ? "吃面。不要问为什么。" : "纺织机没有找到答案，但它记住了你的问题。";
        messages[1] = food ? "今天适合吃热的东西。" : "你正在寻找的答案，已经被写进下一条指令。";
    } else {
        messages[0] = food ? "Eat noodles. Do not ask why." : "The loom found no answer, but it remembered your question.";
        messages[1] = food ? "Eat something warm today." : "The answer you seek has been written into the next prescript.";
    }
    // 内置兜底也保留每类两条及连续去重，只在故障时临时构造快照，不复制进长期正文池。
    PrescriptId &last = LastId(lang,type);
    // 固定保留原兜底UUID，省去运行时旧资源迁移所需的UUIDv5/SHA1生成逻辑。
    // 顺序为中文/英文、食物/纺织机、第一条/第二条；只用于内置内容的连续去重。
    static const char *const ids[2][2][2] = {
        {{"9ba057f6-866c-53b7-80f3-c8b09bd657a5", "d4146656-e98a-5fad-9dfe-2ea1b58bf98c"},
         {"32f93c0c-98c5-519b-bce5-61009aae3347", "bb693e23-a3e2-570c-97b5-e049473978f4"}},
        {{"31d32893-a0d5-5f61-90cb-0a11f15f0392", "518fdb58-ed77-5c97-9c69-6801ab4acfef"},
         {"4c7ad60c-6a0b-5aa2-a1eb-9d207e213212", "2183496d-12f7-599f-bede-f9751322fae5"}}
    };
    const auto &pair = ids[lang == LANG_ZH ? 0 : 1][food ? 0 : 1];
    PrescriptId first, second; first.parse(pair[0]); second.parse(pair[1]);
    const unsigned index = last == first ? 1 : last == second ? 0 : random(2);
    PrescriptContent content;
    if (!SysPrescript::Prepare(messages[index],lang,content).ok()) return false;
    content.id = index ? second : first;
    content.flags&=~PRESCRIPT_APPLY_TARGET; last=content.id;
    out.content=std::move(content); return true;
}
}
void SysOracle::begin(SystemLang_t lang) {
    loaded_lang_=lang; s_loaded=true; s_valid=false; s_rules=Rules{};
    JsonDocument doc(SysPsramJsonAllocator::Instance()); Rules next;
    if (!HAL::FatStorage::IsMountedForEsp() ||
        !SysPrescript::ReadJson(HAL::FatStorage::FileSystem(),TerminalLang::OraclePath(lang),doc)) {
        Serial.println("[纺织机] 指令库不可用，保留原文件，使用内置提示。"); return;
    }
    const bool migrate = doc["schema_version"].is<unsigned>() && doc["schema_version"].as<unsigned>() == 1;
    if ((migrate && !UpgradeLegacy(doc,lang)) || !Parse(doc,lang,next) ||
        (migrate && !SysPrescript::WriteJson(HAL::FatStorage::FileSystem(),TerminalLang::OraclePath(lang),doc))) {
        Serial.println("[纺织机] 内容、规则或迁移保存失败，保留原数据，使用内置提示。"); return;
    }
    s_rules=std::move(next); s_valid=true;
}
bool SysOracle::ensureLoaded(SystemLang_t lang) {
    if (!s_loaded || loaded_lang_!=lang) begin(lang);
    return s_valid && HAL::FatStorage::IsMountedForEsp();
}
bool SysOracle::drawByType(const char *type,SystemLang_t lang,OracleAnswer &out) {
    if (!type || !*type) return false;
    if (!ensureLoaded(lang)) return Fallback(lang,type,out);
    PrescriptId &last=LastId(lang,type);
    uint32_t total=0;
    for (const auto &e : s_rules.entries) if (e.type==type && e.content!=last) total+=e.weight;
    const bool repeat=total==0;
    if (repeat) for (const auto &e : s_rules.entries) if (e.type==type) total+=e.weight;
    if (!total) return Fallback(lang,type,out);
    const uint32_t roll=random(total); uint32_t acc=0;
    for (const auto &e : s_rules.entries) {
        if (e.type!=type || (!repeat && e.content==last)) continue;
        acc+=e.weight;
        if (roll<acc) {
            const auto *content=SysPrescript::Find(s_rules.library,e.content);
            if (!content || !SysPrescript::Prepare(*content,lang,out.content).ok()) return false;
            last=e.content; return true;
        }
    }
    return false;
}
SysOpResult SysOracle::replaceLibrary(const JsonDocument &doc,uint32_t expected) {
    if (!s_valid || !HAL::FatStorage::IsMountedForEsp()) return SysOpResult::Error("STORAGE_UNAVAILABLE");
    if (expected!=s_rules.library.revision || expected==UINT32_MAX || doc["revision"].as<uint32_t>()!=expected+1) return SysOpResult::Error("REVISION_CONFLICT");
    Rules next; if (!Parse(doc,loaded_lang_,next)) return SysOpResult::Error("INVALID_LIBRARY");
    if (next.library.id != s_rules.library.id) return SysOpResult::Error("LIBRARY_CONFLICT");
    if (!SysPrescript::WriteJson(HAL::FatStorage::FileSystem(),TerminalLang::OraclePath(loaded_lang_),doc)) return SysOpResult::Error("SAVE_FAILED");
    s_rules=std::move(next);
    return SysOpResult::Ok("UPDATED");
}
