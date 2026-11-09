/*
【模块职责】共用指令格式和JSON提交机制，不加载全局库、不管理跨库引用或展示状态。
【调用关系】普通、特殊、纺织机分别拥有自己的PrescriptLibrary；所有调用都在主循环串行执行。
*/
#include "sys/resources/sys_prescript.h"
#include "sys/sys_psram_json.h"
#include "sys/sys_protocol.h"
#include <LittleFS.h>
#include <esp_system.h>
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

namespace {
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
}
namespace SysPrescript {
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
    SysOpResult Prepare(const PrescriptContentView &content, SystemLang_t lang, PrescriptContent &out) {
        if (!content.text) return SysOpResult::Error("EMPTY_TEXT");
        if (content.flags & ~(PRESCRIPT_APPLY_TARGET | PRESCRIPT_ALERT)) return SysOpResult::Error("INVALID_FLAGS");
        if (content.audio != AudioAssetId::Invalid && !*AudioName(content.audio)) return SysOpResult::Error("INVALID_AUDIO");
        if (content.title)
            for (const char *p = content.title; *p; ++p)
                if (uint8_t(*p) < 32 || *p == 127) return SysOpResult::Error("INVALID_TITLE");
        const char *title = content.title ? content.title : lang == LANG_ZH ? "【 接收指令 】" : "[ RECEIVE PRESCRIPT ]";
        // 直接复制到最终快照，避免先构造临时拥有文字的内容、再复制一次；失败不碰out。
        PrescriptContent copy;
        copy.id = content.id; copy.color = content.color; copy.audio = content.audio;
        copy.flags = content.flags | PRESCRIPT_HAS_TITLE | PRESCRIPT_HAS_COLOR;
        if (!copy.text.assign(content.text) || !copy.title.assign(title)) return SysOpResult::Error("OUT_OF_MEMORY");
        out = std::move(copy); return SysOpResult::Ok("PREPARED");
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
        if (strcmp(audio, "default")) {
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
        obj["audio"] = item.audio == AudioAssetId::Invalid ? "default" : AudioName(item.audio);
    }

    const PrescriptContent *Find(const PrescriptLibrary &library, const PrescriptId &id) {
        for (const auto &content : library.contents) if (content.id == id) return &content;
        return nullptr;
    }
    bool DecodeLibrary(const JsonDocument &doc, SystemLang_t lang, const char *kind, PrescriptLibrary &out) {
        PrescriptLibrary next;
        if (doc.overflowed() || !TerminalLang::Accepts(lang) || !doc["schema_version"].is<unsigned>() || doc["schema_version"].as<unsigned>() != 2 ||
            !doc["revision"].is<uint32_t>() || strcmp(doc["lang"] | "", TerminalLang::Code(lang)) ||
            strcmp(doc["kind"] | "", kind) || !next.id.parse(doc["library_id"] | "") ||
            !doc["contents"].is<JsonArrayConst>()) return false;
        next.revision = doc["revision"];
        for (JsonVariantConst value : doc["contents"].as<JsonArrayConst>()) {
            PrescriptContent content;
            if (!value.is<JsonObjectConst>() || !Decode(value.as<JsonObjectConst>(), content) || Find(next, content.id)) return false;
            next.contents.push_back(std::move(content));
        }
        out = std::move(next); return true;
    }
    void EncodeLibrary(const PrescriptLibrary &library, SystemLang_t lang, const char *kind, JsonDocument &doc) {
        doc["schema_version"] = 2; doc["library_id"] = library.id.string();
        doc["kind"] = kind; doc["lang"] = TerminalLang::Code(lang); doc["revision"] = library.revision;
        JsonArray contents = doc["contents"].to<JsonArray>();
        for (const auto &content : library.contents) Encode(content, contents.add<JsonObject>());
    }
    bool ReadLegacyCatalog(SystemLang_t lang, JsonDocument &doc) {
        const char *path = lang == LANG_ZH ? "/zh/prescripts.json" : "/en/prescripts.json";
        if (!ReadJson(LittleFS, path, doc) || !doc["schema_version"].is<unsigned>() || doc["schema_version"].as<unsigned>() != 1 ||
            !doc["revision"].is<uint32_t>() || strcmp(doc["lang"] | "", TerminalLang::Code(lang)) ||
            !doc["items"].is<JsonArray>() || !doc["ordinary"].is<JsonArray>() || !doc["references"].is<JsonObject>()) return false;
        PrescriptLibrary checked;
        for (JsonVariantConst value : doc["items"].as<JsonArrayConst>()) {
            PrescriptContent content;
            if (!value.is<JsonObjectConst>() || !Decode(value.as<JsonObjectConst>(), content) || Find(checked, content.id)) return false;
            checked.contents.push_back(std::move(content));
        }
        return true;
    }
    bool MigrateLibrary(SystemLang_t lang, const char *kind, JsonArrayConst ids, JsonDocument &doc) {
        JsonDocument legacy(SysPsramJsonAllocator::Instance());
        if (ids.isNull() || !ReadLegacyCatalog(lang, legacy)) return false;
        JsonArray contents = doc["contents"].to<JsonArray>();
        for (JsonVariantConst value : ids) {
            PrescriptId id;
            if (!id.parse(value.as<const char *>())) return false;
            bool exists = false;
            for (JsonObjectConst item : contents) if (!strcmp(item["id"] | "", id.string().c_str())) { exists = true; break; }
            if (exists) continue; // 同一内容可被多个链条步骤引用，但正文只保存一份。
            bool found = false;
            for (JsonObjectConst item : legacy["items"].as<JsonArrayConst>())
                if (!strcmp(item["id"] | "", id.string().c_str())) { contents.add(item); found = true; break; }
            if (!found) return false;
        }
        doc["schema_version"] = 2; doc["library_id"] = PrescriptId::generate().string();
        doc["kind"] = kind; doc["lang"] = TerminalLang::Code(lang);
        return !doc.overflowed();
    }
}
