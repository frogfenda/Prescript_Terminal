/*
【模块职责】普通库独立拥有内容、增删改和FAT持久化；所有内容直接参与普通抽取。
【迁移约束】新版文件（包括空库）优先，损坏时停用；只在目标完全缺失时导入旧JSON/TXT。
【线程约束】仅启动/主循环调用，编辑成功落盘后保留结果，失败恢复原内存。
*/
#include "sys/resources/sys_ordinary_prescripts.h"
#include "sys/sys_psram_json.h"
#include "hal/hal_fat_storage.h"
#include <LittleFS.h>

namespace {
struct OrdinaryLibrary { PrescriptLibrary data; bool ready = false; };
OrdinaryLibrary s_libraries[2];
OrdinaryLibrary &Pool(SystemLang_t lang) { return s_libraries[lang == LANG_ZH ? 0 : 1]; }

bool Save(SystemLang_t lang) {
    auto &pool = Pool(lang);
    if (!pool.ready || !HAL::FatStorage::IsMountedForEsp() || pool.data.revision == UINT32_MAX) return false;
    JsonDocument doc(SysPsramJsonAllocator::Instance());
    SysPrescript::EncodeLibrary(pool.data, lang, "ordinary", doc);
    doc["revision"] = pool.data.revision + 1;
    if (!SysPrescript::WriteJson(HAL::FatStorage::FileSystem(), TerminalLang::OrdinaryLibraryPath(lang), doc)) return false;
    ++pool.data.revision; return true;
}

bool LoadLanguage(SystemLang_t lang) {
    auto &pool = Pool(lang); pool = OrdinaryLibrary{};
    if (!HAL::FatStorage::IsMountedForEsp()) return false;
    auto &fat = HAL::FatStorage::FileSystem();
    const char *path = TerminalLang::OrdinaryLibraryPath(lang);
    JsonDocument doc(SysPsramJsonAllocator::Instance());
    if (fat.exists(path) || fat.exists(String(path) + ".bak")) {
        pool.ready = SysPrescript::ReadJson(fat, path, doc) && SysPrescript::DecodeLibrary(doc, lang, "ordinary", pool.data);
        return pool.ready;
    }
    // 集中库优先于TXT。旧集中库存在但语义错误时不能降级导入更旧的文本或建立空库。
    const char *old = lang == LANG_ZH ? "/zh/prescripts.json" : "/en/prescripts.json";
    if (LittleFS.exists(old) || LittleFS.exists(String(old) + ".bak")) {
        JsonDocument legacy(SysPsramJsonAllocator::Instance());
        if (!SysPrescript::ReadLegacyCatalog(lang, legacy)) return false;
        doc["revision"] = legacy["revision"];
        if (!SysPrescript::MigrateLibrary(lang, "ordinary", legacy["ordinary"], doc) ||
            !SysPrescript::DecodeLibrary(doc, lang, "ordinary", pool.data)) return false;
    } else {
        pool.data.id = PrescriptId::generate();
        File file = LittleFS.open(TerminalLang::PrescriptPath(lang), "r");
        if (!file) file = LittleFS.open(TerminalLang::LegacyPrescriptPath(lang), "r");
        bool first = true;
        while (file && file.available()) {
            String line = file.readStringUntil('\n'); line.trim();
            if (first && line.startsWith("\xEF\xBB\xBF")) line = line.substring(3);
            first = false; line.trim(); if (line.isEmpty()) continue;
            PrescriptContent item;
            do { item.id = PrescriptId::generate(); } while (SysPrescript::Find(pool.data, item.id));
            if (!item.text.assign(line.c_str())) return false;
            pool.data.contents.push_back(std::move(item));
        }
        file.close();
    }
    pool.ready = true;
    // FAT不会隐式建立多级目录；仅迁移/首次建库时创建本库目录，不触碰其它资源域。
    for (const char *dir : {"/Resources", "/Resources/prescript", "/Resources/prescript/common", "/Resources/prescript/common/text"})
        if (!fat.exists(dir) && !fat.mkdir(dir)) { pool.ready = false; return false; }
    const char *folder = lang == LANG_ZH ? "/Resources/prescript/common/text/zh" : "/Resources/prescript/common/text/en";
    if ((!fat.exists(folder) && !fat.mkdir(folder)) || !Save(lang)) { pool = OrdinaryLibrary{}; return false; }
    return true;
}
}

namespace SysOrdinaryPrescripts {
void Load() {
    for (auto lang : {LANG_ZH, LANG_EN}) if (TerminalLang::Accepts(lang) && !LoadLanguage(lang))
        Serial.printf("[普通指令] %s库不可用，已保留原数据并停用写入。\n", TerminalLang::Code(lang));
}
bool Ready(SystemLang_t lang) { return TerminalLang::Accepts(lang) && Pool(lang).ready && HAL::FatStorage::IsMountedForEsp(); }
const PrescriptContent *Find(SystemLang_t lang, const PrescriptId &id) {
    return Ready(lang) ? SysPrescript::Find(Pool(lang).data, id) : nullptr;
}
size_t Count(SystemLang_t lang) { return Ready(lang) ? Pool(lang).data.contents.size() : 0; }
const PrescriptContent *At(SystemLang_t lang, size_t index) {
    const auto &items = Pool(lang).data.contents;
    return index < Count(lang) ? &items[index] : nullptr;
}
bool Draw(SystemLang_t lang, PrescriptContent &out) {
    const size_t count = Count(lang);
    if (count) return SysPrescript::Prepare(*At(lang, random(count)), lang, out).ok();
    return SysPrescript::Prepare(lang == LANG_ZH ? "错误：中文指令库不可用或为空" : "ERR: EN DB UNAVAILABLE OR EMPTY", lang, out).ok();
}
SysOpResult Add(SystemLang_t lang, const String &text) {
    if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
    if (text.isEmpty()) return SysOpResult::Error("EMPTY_TEXT");
    PrescriptContent item;
    do { item.id = PrescriptId::generate(); } while (Find(lang, item.id));
    if (!item.text.assign(text.c_str())) return SysOpResult::Error("OUT_OF_MEMORY");
    auto &items = Pool(lang).data.contents; items.push_back(std::move(item));
    if (Save(lang)) return SysOpResult::Ok("ADDED");
    items.pop_back(); return SysOpResult::Error("SAVE_FAILED");
}
SysOpResult Update(SystemLang_t lang, const PrescriptContent &content) {
    if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
    JsonDocument check(SysPsramJsonAllocator::Instance()); SysPrescript::Encode(content, check.to<JsonObject>());
    PrescriptContent next;
    if (check.overflowed() || !SysPrescript::Decode(check.as<JsonObjectConst>(), next)) return SysOpResult::Error("INVALID_CONTENT");
    for (auto &item : Pool(lang).data.contents) if (item.id == content.id) {
        PrescriptContent previous = std::move(item); item = std::move(next);
        if (Save(lang)) return SysOpResult::Ok("UPDATED");
        item = std::move(previous); return SysOpResult::Error("SAVE_FAILED");
    }
    return SysOpResult::Error("NOT_FOUND");
}
SysOpResult Delete(SystemLang_t lang, const PrescriptId &requested) {
    const PrescriptId id = requested; // 防止调用者借用库内字段，erase后发生悬空引用。
    if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
    auto &items = Pool(lang).data.contents;
    for (size_t i = 0; i < items.size(); ++i) if (items[i].id == id) {
        PrescriptContent previous = std::move(items[i]); items.erase(items.begin() + i);
        if (Save(lang)) return SysOpResult::Ok("DELETED");
        items.insert(items.begin() + i, std::move(previous)); return SysOpResult::Error("SAVE_FAILED");
    }
    return SysOpResult::Error("NOT_FOUND");
}
SysOpResult ReplaceLibrary(SystemLang_t lang, const JsonDocument &doc, uint32_t expected) {
    if (!Ready(lang)) return SysOpResult::Error("STORAGE_UNAVAILABLE");
    auto &pool = Pool(lang);
    if (expected != pool.data.revision || expected == UINT32_MAX || doc["revision"].as<uint32_t>() != expected + 1)
        return SysOpResult::Error("REVISION_CONFLICT");
    PrescriptLibrary next;
    if (!SysPrescript::DecodeLibrary(doc, lang, "ordinary", next)) return SysOpResult::Error("INVALID_LIBRARY");
    if (next.id != pool.data.id) return SysOpResult::Error("LIBRARY_CONFLICT");
    if (!SysPrescript::WriteJson(HAL::FatStorage::FileSystem(), TerminalLang::OrdinaryLibraryPath(lang), doc))
        return SysOpResult::Error("SAVE_FAILED");
    pool.data = std::move(next); return SysOpResult::Ok("UPDATED");
}
}
