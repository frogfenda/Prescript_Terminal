/*
【模块职责】独立拥有特殊库的内容、触发器和链条；步骤通过本库内容UUID引用，不依赖普通库。
【状态所有权】进度单独按链条UUID/下一步骤UUID持久化；缺失状态的链条默认启用并从第一步开始。
【线程约束】启动或主循环串行调用，预览不推进；状态保存失败则拒绝本次接收。
*/
#include "sys/resources/sys_ordinary_prescripts.h"
#include "sys/resources/sys_specials.h"
#include "sys/sys_constants.h"
#include "sys/sys_psram_json.h"
#include "hal/hal_fat_storage.h"
#include <algorithm>
#include <LittleFS.h>
SysSpecials sysSpecials;
namespace {
struct Pure { PrescriptId id, content; SysPsramString alias; uint16_t probability = 0; };
struct Step { PrescriptId id, content; };
struct Chain {
    PrescriptId id;
    SysPsramString alias, name;
    SysPsramVector<Step> steps;
    uint16_t base = 0, increment = 0, cap = 0;
};
struct Rules { PrescriptLibrary library; SysPsramVector<Pure> pure; SysPsramVector<Chain> chains; };
struct State { PrescriptId chain, next; bool enabled = true, completed = false; };
Rules s_rules;
SysPsramVector<State> s_states;
uint32_t s_revision = 0;
bool s_ready = false;
const char *StatePath(SystemLang_t lang) { return lang == LANG_ZH ? "/zh/prescript_state.json" : "/en/prescript_state.json"; }
bool Matches(const PrescriptId &id, const SysPsramString &alias, const String &query) {
    PrescriptId parsed;
    return (!alias.isEmpty() && alias == query) || (parsed.parse(query.c_str()) && parsed == id);
}
// 只兼容上一版集中正文＋UUID规则，已废弃的无版本JSON不再恢复。
// 完整内容/规则校验成功前只构造内存文档，不写FAT、不修改原集中库。
bool UpgradeLegacy(JsonDocument &doc, SystemLang_t lang) {
    if (!doc["schema_version"].is<unsigned>() || doc["schema_version"].as<unsigned>() != 1 ||
        !doc["revision"].is<uint32_t>() || strcmp(doc["lang"] | "", TerminalLang::Code(lang)) ||
        !doc["pure_specials"].is<JsonArray>() || !doc["character_chains"].is<JsonArray>()) return false;
    JsonDocument references(SysPsramJsonAllocator::Instance()); JsonArray ids = references.to<JsonArray>();
    for (JsonObjectConst pure : doc["pure_specials"].as<JsonArrayConst>()) ids.add(pure["content_id"]);
    for (JsonObjectConst chain : doc["character_chains"].as<JsonArrayConst>())
        for (JsonObjectConst step : chain["steps"].as<JsonArrayConst>()) ids.add(step["content_id"]);
    return !references.overflowed() && SysPrescript::MigrateLibrary(lang, "special", ids, doc);
}
bool Parse(const JsonDocument &doc, SystemLang_t lang, Rules &out) {
    if (!doc["pure_specials"].is<JsonArrayConst>() || !doc["character_chains"].is<JsonArrayConst>()) return false;
    Rules rules;
    if (!SysPrescript::DecodeLibrary(doc, lang, "special", rules.library)) return false;
    SysPsramVector<PrescriptId> identities; SysPsramTextList aliases;
    auto identity = [&](JsonVariantConst value, PrescriptId &id) {
        if (!id.parse(value.as<const char *>()) || std::find(identities.begin(),identities.end(),id) != identities.end()) return false;
        identities.push_back(id); return true;
    };
    auto alias = [&](JsonVariantConst value, SysPsramString &text) {
        if (value.isNull()) return true;
        if (!value.is<const char *>() || !text.assign(value.as<const char *>())) return false;
        if (text.isEmpty()) return true;
        for (const auto &a : aliases) if (a == text) return false;
        aliases.push_back(text); return true;
    };
    auto content = [&](JsonVariantConst value, PrescriptId &id) {
        return id.parse(value.as<const char *>()) && SysPrescript::Find(rules.library,id);
    };
    uint32_t maximum = 0;
    for (JsonObjectConst obj : doc["pure_specials"].as<JsonArrayConst>()) {
        Pure p;
        if (!identity(obj["id"],p.id) || !content(obj["content_id"],p.content) || !alias(obj["legacy_id"],p.alias) ||
            !obj["probability_bp"].is<uint16_t>() || obj["probability_bp"].as<unsigned>() > 10000) return false;
        p.probability = obj["probability_bp"]; maximum += p.probability; rules.pure.push_back(std::move(p));
    }
    for (JsonObjectConst obj : doc["character_chains"].as<JsonArrayConst>()) {
        Chain c;
        if (rules.chains.size() >= PrescriptConst::MAX_CHAR_CHAINS || !identity(obj["id"],c.id) ||
            !alias(obj["legacy_id"],c.alias) || !obj["name"].is<const char *>() || !c.name.assign(obj["name"].as<const char *>()) ||
            !obj["steps"].is<JsonArrayConst>()) return false;
        for (const char *key : {"base_bp","increment_bp","max_bp"})
            if (!obj[key].is<uint16_t>() || obj[key].as<unsigned>() > 10000) return false;
        c.base = obj["base_bp"]; c.increment = obj["increment_bp"]; c.cap = obj["max_bp"];
        if (c.cap < c.base) return false;
        for (JsonObjectConst step : obj["steps"].as<JsonArrayConst>()) {
            Step s;
            if (c.steps.size() >= 255 || !identity(step["id"],s.id) || !content(step["content_id"],s.content)) return false;
            c.steps.push_back(s);
        }
        if (c.steps.empty()) return false;
        maximum += std::min<uint32_t>(c.cap,c.base + (c.steps.size()-1)*c.increment);
        rules.chains.push_back(std::move(c));
    }
    // 万分位绝对概率不能按权重归一化；预算溢出会按顺序吞掉后面的触发器，因此整体拒绝。
    if (maximum > 10000) return false;
    out = std::move(rules); return true;
}
const State *GetState(const PrescriptId &id) {
    for (const auto &state : s_states) if (state.chain == id) return &state;
    return nullptr;
}
size_t Position(const Chain &chain, const State &state) {
    if (state.completed) return chain.steps.size();
    for (size_t i=0;i<chain.steps.size();++i) if (chain.steps[i].id == state.next) return i;
    return chain.steps.size();
}
bool SaveState(SystemLang_t lang) {
    if (s_revision == UINT32_MAX) return false;
    JsonDocument doc(SysPsramJsonAllocator::Instance());
    doc["schema_version"]=1; doc["revision"]=s_revision+1; doc["lang"]=TerminalLang::Code(lang);
    JsonArray array=doc["chains"].to<JsonArray>();
    for (const auto &s : s_states) {
        JsonObject o=array.add<JsonObject>(); o["chain_id"]=s.chain.string();
        if (!s.completed) o["next_step_id"]=s.next.string();
        o["completed"]=s.completed; o["enabled"]=s.enabled;
    }
    if (!SysPrescript::WriteJson(LittleFS,StatePath(lang),doc)) return false;
    ++s_revision; return true;
}
bool LoadState(SystemLang_t lang) {
    s_states.clear(); s_revision=0;
    const bool exists=LittleFS.exists(StatePath(lang)) || LittleFS.exists(String(StatePath(lang))+".bak");
    if (exists) {
        JsonDocument doc(SysPsramJsonAllocator::Instance());
        if (!SysPrescript::ReadJson(LittleFS,StatePath(lang),doc) || (!doc["schema_version"].is<unsigned>() || doc["schema_version"].as<unsigned>()!=1) ||
            !doc["revision"].is<uint32_t>() || strcmp(doc["lang"]|"",TerminalLang::Code(lang)) || !doc["chains"].is<JsonArray>()) return false;
        s_revision=doc["revision"];
        for (JsonObjectConst obj : doc["chains"].as<JsonArrayConst>()) {
            State state;
            if (!state.chain.parse(obj["chain_id"]|"") || GetState(state.chain) ||
                !obj["completed"].is<bool>() || !obj["enabled"].is<bool>()) return false;
            state.completed=obj["completed"]; state.enabled=obj["enabled"];
            if (!state.completed && !state.next.parse(obj["next_step_id"]|"")) return false;
            s_states.push_back(state);
        }
    }
    bool added=false;
    for (const auto &chain : s_rules.chains) {
        const State *state=GetState(chain.id);
        if (state) {
            if (!state->completed && Position(chain,*state)==chain.steps.size()) return false;
            continue;
        }
        // 新链条只按自身UUID建立进度，不再读取配置中的旧人物下标或开关。
        State initial; initial.chain=chain.id; initial.next=chain.steps.front().id;
        s_states.push_back(initial); added=true;
    }
    return (!added && exists) || SaveState(lang);
}
bool Snapshot(SystemLang_t lang,const PrescriptId &id,PrescriptContent &out) {
    const auto *content=SysPrescript::Find(s_rules.library,id);
    return content && SysPrescript::Prepare(*content,lang,out).ok();
}
}
void SysSpecials::begin(SystemLang_t lang) {
    current_lang_=lang; s_ready=false; s_rules=Rules{}; s_states.clear();
    JsonDocument doc(SysPsramJsonAllocator::Instance()); Rules parsed;
    if (!HAL::FatStorage::IsMountedForEsp() ||
        !SysPrescript::ReadJson(HAL::FatStorage::FileSystem(),TerminalLang::SpecialsPath(lang),doc)) {
        Serial.println("[特殊指令] 指令库不可用，保留原文件；本次仅允许抽取普通指令。"); return;
    }
    const bool migrate = doc["schema_version"].is<unsigned>() && doc["schema_version"].as<unsigned>() == 1;
    if ((migrate && !UpgradeLegacy(doc,lang)) || !Parse(doc,lang,parsed) ||
        (migrate && !SysPrescript::WriteJson(HAL::FatStorage::FileSystem(),TerminalLang::SpecialsPath(lang),doc))) {
        Serial.println("[特殊指令] 内容、规则或迁移保存失败，已停用特殊抽取并保留原数据。"); return;
    }
    s_rules=std::move(parsed);
    if (!LoadState(lang)) { Serial.println("[特殊指令] 进度文件不可用，已停用特殊抽取，未重置旧进度。"); return; }
    s_ready=true;
}
bool SysSpecials::selectRandom(PrescriptContent &out,PrescriptSelection &selection) const {
    PrescriptSelection selected; selected.lang=current_lang_; selected.revision=s_revision; selected.rules_revision=s_rules.library.revision;
    uint32_t cumulative=0; const unsigned roll=random(10000);
    if (s_ready && HAL::FatStorage::IsMountedForEsp()) {
        for (const auto &chain : s_rules.chains) {
            const State *state=GetState(chain.id);
            if (!state || !state->enabled || state->completed) continue;
            const size_t pos=Position(chain,*state); if (pos>=chain.steps.size()) continue;
            cumulative+=std::min<uint32_t>(chain.cap,chain.base+pos*chain.increment);
            if (roll<cumulative) {
                if (!Snapshot(current_lang_,chain.steps[pos].content,out)) return false;
                selected.chain=chain.id; selected.expected=state->next; selected.step=chain.steps[pos].id;
                selection=selected; return true;
            }
        }
        for (const auto &pure : s_rules.pure) {
            cumulative+=pure.probability;
            if (roll<cumulative) { if (!Snapshot(current_lang_,pure.content,out)) return false; selection=selected; return true; }
        }
    }
    if (!SysOrdinaryPrescripts::Draw(current_lang_,out)) return false;
    selection=selected; return true;
}
bool SysSpecials::selectForced(const String &id,PrescriptContent &out,PrescriptSelection &selection) const {
    PrescriptSelection selected; selected.lang=current_lang_; selected.revision=s_revision; selected.rules_revision=s_rules.library.revision;
    if (s_ready && HAL::FatStorage::IsMountedForEsp()) {
        for (const auto &pure : s_rules.pure) if (Matches(pure.id,pure.alias,id)) {
            if (!Snapshot(current_lang_,pure.content,out)) return false;
            selection=selected; return true;
        }
        for (const auto &chain : s_rules.chains) if (Matches(chain.id,chain.alias,id)) {
            const State *state=GetState(chain.id); if (!state || !Snapshot(current_lang_,chain.steps[0].content,out)) return false;
            selected.chain=chain.id; selected.expected=state->next; selected.step=chain.steps[0].id;
            selection=selected; return true;
        }
    }
    const String message=current_lang_==LANG_ZH ? String("未找到对应条目：ID [")+id+"]" : String("No matching entry: ID [")+id+"]";
    PrescriptContentView content(message.c_str()); content.color=0xF800;
    content.title=current_lang_==LANG_ZH ? "【 指令检索失败 】" : "[ PRESCRIPT NOT FOUND ]";
    content.flags=PRESCRIPT_ALERT;
    if (!SysPrescript::Prepare(content,current_lang_,out).ok()) return false;
    selection=selected; return true;
}
bool SysSpecials::commit(const PrescriptSelection &selection) {
    if (selection.chain.empty()) return true;
    if (!s_ready || selection.lang!=current_lang_ || selection.revision!=s_revision || selection.rules_revision!=s_rules.library.revision) return false;
    for (const auto &chain : s_rules.chains) if (chain.id==selection.chain) {
        for (auto &state : s_states) if (state.chain==chain.id) {
            if (state.next!=selection.expected) return false;
            size_t pos=0; while (pos<chain.steps.size() && chain.steps[pos].id!=selection.step) ++pos;
            if (pos==chain.steps.size()) return false;
            const State previous=state;
            state.completed=++pos>=chain.steps.size(); state.next=state.completed ? PrescriptId{} : chain.steps[pos].id;
            if (SaveState(current_lang_)) return true;
            state=previous; return false;
        }
    }
    return false;
}
bool SysSpecials::hasID(const String &id) const {
    if (!s_ready || !HAL::FatStorage::IsMountedForEsp()) return false;
    for (const auto &p : s_rules.pure) if (Matches(p.id,p.alias,id)) return true;
    for (const auto &c : s_rules.chains) if (Matches(c.id,c.alias,id)) return true;
    return false;
}
bool SysSpecials::textByID(const String &id,String &out) const {
    if (!s_ready || !HAL::FatStorage::IsMountedForEsp()) return false;
    for (const auto &p : s_rules.pure) if (Matches(p.id,p.alias,id)) {
        const auto *item=SysPrescript::Find(s_rules.library,p.content); if (!item) return false;
        out=item->text.c_str(); return true;
    }
    for (const auto &c : s_rules.chains) if (Matches(c.id,c.alias,id)) {
        const State *state=GetState(c.id); if (!state) return false;
        const size_t pos=Position(c,*state);
        if (pos==c.steps.size()) { out=current_lang_==LANG_ZH ? "[ 观测日志已完结，目标数据归档完毕 ]" : "[ OBSERVATION LOG COMPLETE. TARGET DATA ARCHIVED. ]"; return true; }
        const auto *item=SysPrescript::Find(s_rules.library,c.steps[pos].content); if (!item) return false;
        out=item->text.c_str(); return true;
    }
    return false;
}
bool SysSpecials::metaAt(uint16_t index,JsonDocument &out) const {
    if (!s_ready || !HAL::FatStorage::IsMountedForEsp()) return false;
    PrescriptId content;
    if (index<s_rules.pure.size()) {
        const auto &p=s_rules.pure[index]; content=p.content;
        out["kind"]="P"; out["id"]=p.alias.isEmpty() ? p.id.string() : String(p.alias.c_str());
        out["name"]=out["id"]; out["prob"]=p.probability; out["prog"]=1; out["total"]=1; out["enabled"]=true;
    } else {
        const size_t at=index-s_rules.pure.size(); if (at>=s_rules.chains.size()) return false;
        const auto &c=s_rules.chains[at]; const State *state=GetState(c.id); if (!state) return false;
        const size_t pos=Position(c,*state); content=c.steps[pos<c.steps.size() ? pos : 0].content;
        out["kind"]="C"; out["id"]=c.alias.isEmpty() ? c.id.string() : String(c.alias.c_str());
        out["name"]=c.name.c_str(); out["prob"]=c.base; out["prog"]=pos; out["total"]=c.steps.size(); out["enabled"]=state->enabled;
    }
    PrescriptContent snapshot; if (!Snapshot(current_lang_,content,snapshot)) return false;
    char color[8]; snprintf(color,sizeof(color),"0x%04X",snapshot.color);
    out["color"]=color; out["title"]=snapshot.title.c_str(); return true;
}
SysOpResult SysSpecials::replaceLibrary(const JsonDocument &doc,uint32_t expected) {
    if (!s_ready || !HAL::FatStorage::IsMountedForEsp()) return SysOpResult::Error("STORAGE_UNAVAILABLE");
    if (expected!=s_rules.library.revision || expected==UINT32_MAX || doc["revision"].as<uint32_t>()!=expected+1) return SysOpResult::Error("REVISION_CONFLICT");
    Rules next; if (!Parse(doc,current_lang_,next)) return SysOpResult::Error("INVALID_LIBRARY");
    if (next.library.id != s_rules.library.id) return SysOpResult::Error("LIBRARY_CONFLICT");
    // 正在等待的步骤不能被隐式删除/重置；编辑器须先显式处理进度。
    for (const auto &c : next.chains) {
        const State *state=GetState(c.id);
        if (state && !state->completed && Position(c,*state)==c.steps.size()) return SysOpResult::Error("STATE_CONFLICT");
    }
    // 新链条先建立状态再发布规则；失败最多留下尚未使用的状态，不改变旧链条进度。
    bool added = false;
    const size_t previous_states = s_states.size();
    for (const auto &c : next.chains) if (!GetState(c.id)) {
        State initial; initial.chain = c.id; initial.next = c.steps.front().id;
        s_states.push_back(initial); added = true;
    }
    if (added && !SaveState(current_lang_)) {
        s_states.resize(previous_states); return SysOpResult::Error("SAVE_FAILED");
    }
    // 内容和规则是同一文件提交；保存失败不发布新内存库，已创建状态最多成为未使用记录。
    if (!SysPrescript::WriteJson(HAL::FatStorage::FileSystem(),TerminalLang::SpecialsPath(current_lang_),doc)) return SysOpResult::Error("SAVE_FAILED");
    s_rules=std::move(next);
    return SysOpResult::Ok("UPDATED");
}
