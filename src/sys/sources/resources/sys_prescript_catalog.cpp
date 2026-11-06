/*
【模块职责】统一内容库的加载、旧文本迁移、引用保护和可恢复保存。
【事务边界】普通候选与内容同文件提交；特殊/纺织机先保存内容及引用保护，再发布规则，
掉电最多留下未使用内容，不会发布指向未保存内容的引用。所有写入在主循环串行执行。
*/
#include "sys/resources/sys_prescript_catalog.h"
#include "sys/resources/sys_specials.h"
#include "sys/resources/sys_oracle.h"
#include "sys/sys_psram_json.h"
#include "sys/sys_protocol.h"
#include <esp_system.h>
#include <algorithm>

bool PrescriptId::empty() const
{ for (auto b : bytes) if (b) return false; return true; }
bool PrescriptId::operator==(const PrescriptId &other) const
{ return memcmp(bytes, other.bytes, sizeof(bytes)) == 0; }
bool PrescriptId::parse(const char *uuid)
{
    if (!uuid || strlen(uuid) != 36) return false;
    PrescriptId parsed;
    size_t digit = 0;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (uuid[i] != '-') return false; continue; }
        const char c = uuid[i];
        int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (value < 0) return false;
        parsed.bytes[digit / 2] |= value << ((digit % 2) ? 0 : 4); ++digit;
    }
    if (parsed.empty()) return false;
    *this = parsed; return true;
}
String PrescriptId::string() const
{
    char text[37];
    snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
    return String(text);
}
PrescriptId PrescriptId::generate()
{
    PrescriptId id; esp_fill_random(id.bytes, sizeof(id.bytes));
    id.bytes[6] = (id.bytes[6] & 15) | 0x40; id.bytes[8] = (id.bytes[8] & 63) | 0x80; return id;
}

namespace
{
    struct ReferenceSet { SysPsramString module; SysPsramVector<PrescriptId> ids; };
    struct Catalog {
        SysPsramVector<PrescriptContent> items;
        SysPsramVector<PrescriptId> ordinary;
        SysPsramVector<ReferenceSet> references;
        uint32_t revision = 0;
        bool ready = false;
        bool references_dirty = false;
        bool deletion_blocked = false;
    };
    Catalog s_catalogs[2];
    PrescriptContent s_presented;
    Catalog &Pool(SystemLang_t lang) { return s_catalogs[lang == LANG_ZH ? 0 : 1]; }
    const char *Path(SystemLang_t lang) { return lang == LANG_ZH ? "/zh/prescripts.json" : "/en/prescripts.json"; }
    const char *AudioName(AudioAssetId id) {
        switch (id) {
        case AudioAssetId::CoinHeads: return "heads";
        case AudioAssetId::CoinTails: return "tails";
        case AudioAssetId::Ahab: return "Ahab";
        case AudioAssetId::Procedure: return "procedure";
        case AudioAssetId::Final: return "final";
        default: return "";
        }
    }
    bool Copy(const PrescriptContent &src, PrescriptContent &dst) {
        PrescriptContent copy;
        copy.id = src.id; copy.color = src.color; copy.audio = src.audio; copy.flags = src.flags;
        if (!copy.text.assign(src.text.c_str(), src.text.length()) || !copy.title.assign(src.title.c_str(), src.title.length())) return false;
        dst = std::move(copy); return true;
    }
    bool Save(SystemLang_t lang) {
        Catalog &pool = Pool(lang);
        if (!pool.ready || pool.revision == UINT32_MAX) return false;
        JsonDocument doc(SysPsramJsonAllocator::Instance());
        doc["schema_version"] = 1; doc["revision"] = pool.revision + 1; doc["lang"] = TerminalLang::Code(lang);
        JsonArray items = doc["items"].to<JsonArray>();
        for (const auto &item : pool.items) SysPrescriptCatalog::Encode(item, items.add<JsonObject>());
        JsonArray ordinary = doc["ordinary"].to<JsonArray>();
        for (const auto &id : pool.ordinary) ordinary.add(id.string());
        JsonObject refs = doc["references"].to<JsonObject>();
        for (const auto &set : pool.references) {
            JsonArray ids = refs[set.module.c_str()].to<JsonArray>();
            for (const auto &id : set.ids) ids.add(id.string());
        }
        if (!SysPrescriptCatalog::WriteJson(LittleFS, Path(lang), doc)) return false;
        ++pool.revision; pool.references_dirty = false; return true;
    }
    bool Contains(const SysPsramVector<PrescriptId> &ids, const PrescriptId &id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    bool ParseCatalog(SystemLang_t lang, JsonDocument &doc, Catalog &next) {
        if ((!doc["schema_version"].is<unsigned>() || doc["schema_version"].as<unsigned>() != 1) || !doc["revision"].is<uint32_t>() ||
            strcmp(doc["lang"] | "", TerminalLang::Code(lang)) ||
            !doc["items"].is<JsonArray>() || !doc["ordinary"].is<JsonArray>() || !doc["references"].is<JsonObject>()) return false;
        next.revision = doc["revision"];
        for (JsonObjectConst obj : doc["items"].as<JsonArrayConst>()) {
            PrescriptContent item;
            if (!SysPrescriptCatalog::Decode(obj, item)) return false;
            for (const auto &existing : next.items) if (existing.id == item.id) return false;
            next.items.push_back(std::move(item));
        }
        auto readIds = [&next](JsonArrayConst array, SysPsramVector<PrescriptId> &ids) {
            for (JsonVariantConst value : array) {
                PrescriptId id; if (!id.parse(value.as<const char *>())) return false;
                bool found = false;
                for (const auto &item : next.items) if (item.id == id) { found = true; break; }
                if (!found || Contains(ids, id)) return false;
                ids.push_back(id);
            }
            return true;
        };
        if (!readIds(doc["ordinary"], next.ordinary)) return false;
        for (JsonPairConst pair : doc["references"].as<JsonObjectConst>()) {
            ReferenceSet refs;
            if (!pair.value().is<JsonArrayConst>() || !refs.module.assign(pair.key().c_str()) || !readIds(pair.value(), refs.ids)) return false;
            next.references.push_back(std::move(refs));
        }
        next.ready = true; return true;
    }
    void LoadLanguage(SystemLang_t lang) {
        Catalog &pool = Pool(lang); pool = Catalog{};
        JsonDocument doc(SysPsramJsonAllocator::Instance());
        const String backup = String(Path(lang)) + ".bak";
        if (LittleFS.exists(Path(lang)) || LittleFS.exists(backup)) {
            Catalog next;
            if (SysPrescriptCatalog::ReadJson(LittleFS, Path(lang), doc) && ParseCatalog(lang, doc, next)) {
                pool = std::move(next); return;
            }
            Serial.printf("[指令库] %s库损坏或版本不支持，已保留原文件并停用写入。\n", TerminalLang::Code(lang));
            return;
        }
        // 只有新库完全不存在才迁移TXT；空JSON库也是有效的用户选择，不得重新导入已删除内容。
        File file = LittleFS.open(TerminalLang::PrescriptPath(lang), "r");
        if (!file) file = LittleFS.open(TerminalLang::LegacyPrescriptPath(lang), "r");
        size_t index = 0;
        while (file && file.available()) {
            String line = file.readStringUntil('\n'); line.trim();
            if (!index && line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);
            line.trim();
            if (line.isEmpty()) continue;
            // 本地TXT没有来源身份，不能按行号生成跨设备相同UUID；同一位置可能是完全不同的自定义指令。
            PrescriptContent item;
            do { item.id = PrescriptId::generate(); } while (SysPrescriptCatalog::Find(lang, item.id));
            ++index;
            if (!item.text.assign(line.c_str())) return;
            pool.ordinary.push_back(item.id); pool.items.push_back(std::move(item));
        }
        file.close(); pool.ready = true;
        if (!Save(lang)) { pool.ready = false; Serial.println("[指令库] 旧文本迁移保存失败，原TXT已保留。"); }
    }
}

namespace SysPrescriptCatalog
{
    bool ReadJson(fs::FS &fs, const char *path, JsonDocument &out) {
        // 正式版本优先；备份仍保留可读，成功后不删除，写入时才轮换。
        const String candidates[] = {String(path), String(path) + ".bak"};
        for (const String &candidate : candidates) {
            File file = fs.open(candidate, "r");
            if (!file) continue;
            out.clear(); const auto error = deserializeJson(out, file); file.close();
            if (!error && !out.overflowed()) return true;
        }
        out.clear(); return false;
    }
    bool WriteJson(fs::FS &fs, const char *path, const JsonDocument &document) {
        if (document.overflowed()) return false;
        const String tmp = String(path) + ".tmp", backup = String(path) + ".bak";
        File file = fs.open(tmp, "w"); if (!file) return false;
        const size_t expected = measureJson(document), written = serializeJson(document, file);
        file.flush(); const size_t size = file.size(); file.close();
        if (written != expected || size != expected) { fs.remove(tmp); return false; }
        // 回读验证短写/损坏；解析树在离开此作用域即释放，避免发布半份新数据。
        { JsonDocument check(SysPsramJsonAllocator::Instance());
          File verify = fs.open(tmp, "r");
          const bool valid = verify && !deserializeJson(check, verify) && !check.overflowed(); verify.close();
          if (!valid) { fs.remove(tmp); return false; } }
        if (fs.exists(path)) {
            // 仅当正式文件本身有效时轮换，不能用损坏的正式文件覆盖唯一有效备份。
            JsonDocument old(SysPsramJsonAllocator::Instance()); File current = fs.open(path, "r");
            const bool valid = current && !deserializeJson(old, current); current.close();
            if (valid) {
                if (fs.exists(backup) && !fs.remove(backup)) return false;
                if (!fs.rename(path, backup)) return false;
            } else if (!fs.remove(path)) return false;
        }
        if (!fs.rename(tmp, path)) return false;
        return true;
    }
    void Load() {
        for (SystemLang_t lang : {LANG_ZH, LANG_EN}) if (TerminalLang::Accepts(lang)) LoadLanguage(lang);
    }
    bool Ready(SystemLang_t lang) { return TerminalLang::Accepts(lang) && Pool(lang).ready; }
    const PrescriptContent *Find(SystemLang_t lang, const PrescriptId &id) {
        for (const auto &item : Pool(lang).items) if (item.id == id) return &item;
        return nullptr;
    }
    size_t OrdinaryCount(SystemLang_t lang) { return Pool(lang).ordinary.size(); }
    const PrescriptContent *OrdinaryAt(SystemLang_t lang, size_t index) {
        const auto &ids = Pool(lang).ordinary; return index < ids.size() ? Find(lang, ids[index]) : nullptr;
    }
    bool Snapshot(const PrescriptContent &content, SystemLang_t lang, PrescriptContent &out) {
        PrescriptContent copy; if (!Copy(content, copy)) return false;
        if (!(copy.flags & PRESCRIPT_HAS_TITLE) && !copy.title.assign(lang == LANG_ZH ? "【 接收指令 】" : "[ RECEIVE PRESCRIPT ]")) return false;
        if (!(copy.flags & PRESCRIPT_HAS_COLOR)) copy.color = 0x07FF;
        copy.flags |= PRESCRIPT_HAS_TITLE | PRESCRIPT_HAS_COLOR;
        out = std::move(copy); return true;
    }
    bool Custom(SystemLang_t lang, const char *text, uint16_t color, const char *title, PrescriptContent &out) {
        PrescriptContent content; content.color = color; content.flags |= PRESCRIPT_HAS_COLOR;
        if (!content.text.assign(text) || !content.title.assign(title)) return false;
        if (title && *title) content.flags |= PRESCRIPT_HAS_TITLE;
        return Snapshot(content, lang, out);
    }
    bool DrawOrdinary(SystemLang_t lang, PrescriptContent &out) {
        const size_t count = OrdinaryCount(lang);
        if (count) return Snapshot(*OrdinaryAt(lang, random(count)), lang, out);
        return Custom(lang, lang == LANG_ZH ? "错误：中文指令库为空" : "ERR: EN DB EMPTY", 0x07FF, nullptr, out);
    }
    SysOpResult AddOrdinary(SystemLang_t lang, const String &text) {
        if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
        if (text.isEmpty()) return SysOpResult::Error("EMPTY_TEXT");
        Catalog &pool = Pool(lang); PrescriptContent item;
        do { item.id = PrescriptId::generate(); } while (Find(lang, item.id));
        if (!item.text.assign(text.c_str())) return SysOpResult::Error("OUT_OF_MEMORY");
        const PrescriptId id = item.id; pool.items.push_back(std::move(item)); pool.ordinary.push_back(id);
        if (!Save(lang)) { pool.items.pop_back(); pool.ordinary.pop_back(); return SysOpResult::Error("SAVE_FAILED"); }
        return SysOpResult::Ok("ADDED");
    }
    SysOpResult Update(SystemLang_t lang, const PrescriptContent &content) {
        if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
        // 编解码校验复用正式文件契约，不能让不可持久化的音频编号或空正文进入库。
        JsonDocument check(SysPsramJsonAllocator::Instance()); Encode(content, check.to<JsonObject>());
        PrescriptContent next;
        if (!Decode(check.as<JsonObjectConst>(), next)) return SysOpResult::Error("INVALID_CONTENT");
        for (auto &item : Pool(lang).items) if (item.id == content.id) {
            PrescriptContent previous = std::move(item); item = std::move(next);
            if (Save(lang)) return SysOpResult::Ok("UPDATED");
            item = std::move(previous); return SysOpResult::Error("SAVE_FAILED");
        }
        return SysOpResult::Error("NOT_FOUND");
    }
    SysOpResult Delete(SystemLang_t lang, const PrescriptId &requested_id) {
        const PrescriptId id = requested_id; // 调用者可能传入库内字段，erase后不得继续借用。
        if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
        Catalog &pool = Pool(lang);
        // FAT可经USB独立编辑，另一个语言的规则也可能比启动时保存的引用集合更新。
        // 由各规则模块只读验证自身格式，内容库不解释链条/分类字段。未知/损坏版本一律拒删。
        SysPsramVector<PrescriptId> special_refs, oracle_refs;
        if (!SysSpecials::references(lang, special_refs) || !SysOracle::references(lang, oracle_refs) ||
            !SetReferences(lang, "special", special_refs) || !SetReferences(lang, "oracle", oracle_refs))
            return SysOpResult::Error("REFERENCES_UNAVAILABLE");
        bool special = false, oracle = false;
        for (const auto &refs : pool.references) {
            special |= refs.module == "special"; oracle |= refs.module == "oracle";
        }
        if (pool.deletion_blocked || !special || !oracle) return SysOpResult::Error("REFERENCES_UNAVAILABLE");
        for (const auto &refs : pool.references) if (Contains(refs.ids, id)) return SysOpResult::Error("IN_USE", refs.module.c_str());
        for (size_t i = 0; i < pool.items.size(); ++i) if (pool.items[i].id == id) {
            auto candidate = std::find(pool.ordinary.begin(), pool.ordinary.end(), id);
            const size_t pos = candidate - pool.ordinary.begin(); const bool ordinary = candidate != pool.ordinary.end();
            PrescriptContent previous = std::move(pool.items[i]); pool.items.erase(pool.items.begin() + i);
            if (ordinary) pool.ordinary.erase(candidate);
            if (Save(lang)) return SysOpResult::Ok("DELETED");
            pool.items.insert(pool.items.begin() + i, std::move(previous));
            if (ordinary) pool.ordinary.insert(pool.ordinary.begin() + pos, id);
            return SysOpResult::Error("SAVE_FAILED");
        }
        return SysOpResult::Error("NOT_FOUND");
    }
    void BlockDeletion(SystemLang_t lang) { Pool(lang).deletion_blocked = true; }
    SysOpResult RemoveOrdinary(SystemLang_t lang, const PrescriptId &requested_id) {
        const PrescriptId id = requested_id;
        if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
        auto &ids = Pool(lang).ordinary;
        const auto at = std::find(ids.begin(), ids.end(), id);
        if (at == ids.end()) return SysOpResult::Error("NOT_FOUND");
        const size_t pos = at - ids.begin(); ids.erase(at);
        if (Save(lang)) return SysOpResult::Ok("REMOVED");
        ids.insert(ids.begin() + pos, id); return SysOpResult::Error("SAVE_FAILED");
    }
    bool SetReferences(SystemLang_t lang, const char *module, const SysPsramVector<PrescriptId> &ids) {
        if (!Ready(lang) || !module || !*module) return false;
        for (const auto &id : ids) if (!Find(lang, id)) return false;
        auto &sets = Pool(lang).references;
        for (auto &set : sets) if (set.module == module) {
            if (set.ids == ids) return !Pool(lang).references_dirty || Save(lang);
            auto previous = set.ids;
            set.ids = ids;
            if (Save(lang)) return true;
            for (const auto &id : previous) if (!Contains(set.ids, id)) set.ids.push_back(id);
            Pool(lang).references_dirty = true;
            Serial.println("[指令库] 引用保护保存失败，内存仍保留删除保护。"); return false;
        }
        ReferenceSet set;
        if (!set.module.assign(module)) return false;
        set.ids = ids; sets.push_back(std::move(set));
        if (Save(lang)) return true;
        Pool(lang).references_dirty = true;
        Serial.println("[指令库] 引用保护保存失败，内存仍保留删除保护。"); return false;
    }
    bool Decode(JsonObjectConst obj, PrescriptContent &out) {
        PrescriptContent item;
        if (!item.id.parse(obj["id"] | "") || !obj["text"].is<const char *>() || !strlen(obj["text"]) ||
            !item.text.assign(obj["text"].as<const char *>())) return false;
        if (!obj["title"].isNull()) {
            if (!obj["title"].is<const char *>() || !item.title.assign(obj["title"].as<const char *>())) return false;
            for (const char *p = item.title.c_str(); *p; ++p) if (uint8_t(*p) < 32 || *p == 127) return false;
            item.flags |= PRESCRIPT_HAS_TITLE;
        }
        if (!obj["color"].isNull()) {
            if (!SysProtocol_ParseColor(obj["color"].as<const char *>(), item.color)) return false;
            item.flags |= PRESCRIPT_HAS_COLOR;
        }
        for (const char *flag : {"apply_target", "alert"}) if (!obj[flag].isNull() && !obj[flag].is<bool>()) return false;
        if (!(obj["apply_target"] | true)) item.flags &= ~PRESCRIPT_APPLY_TARGET;
        if (obj["alert"] | false) item.flags |= PRESCRIPT_ALERT;
        const char *audio = obj["audio"] | "default";
        if (!obj["audio"].isNull() && !obj["audio"].is<const char *>()) return false;
        if (!strcmp(audio, "none")) item.flags |= PRESCRIPT_SILENT;
        else if (strcmp(audio, "default")) {
            bool found = false;
            for (auto id : {AudioAssetId::CoinHeads, AudioAssetId::CoinTails, AudioAssetId::Ahab, AudioAssetId::Procedure, AudioAssetId::Final})
                if (!strcmp(audio, AudioName(id))) { item.audio = id; found = true; break; }
            if (!found) return false;
        }
        out = std::move(item); return true;
    }
    void Encode(const PrescriptContent &item, JsonObject obj) {
        obj["id"] = item.id.string(); obj["text"] = item.text.c_str();
        if (item.flags & PRESCRIPT_HAS_TITLE) obj["title"] = item.title.c_str();
        if (item.flags & PRESCRIPT_HAS_COLOR) {
            const uint16_t c = item.color;
            // RGB565展开到RGB888后再次解析必须逐位还原，扩展低位使白色仍输出#FFFFFF。
            const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            char color[8]; snprintf(color, sizeof(color), "#%02X%02X%02X", (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
            obj["color"] = color;
        }
        obj["apply_target"] = bool(item.flags & PRESCRIPT_APPLY_TARGET); obj["alert"] = bool(item.flags & PRESCRIPT_ALERT);
        obj["audio"] = item.flags & PRESCRIPT_SILENT ? "none" : item.audio == AudioAssetId::Invalid ? "default" : AudioName(item.audio);
    }
    void Present(PrescriptContent &&snapshot) { s_presented = std::move(snapshot); }
    const PrescriptContent &Presented() { return s_presented; }
}
