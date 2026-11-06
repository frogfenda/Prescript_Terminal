/*
【模块职责】独立指令内容库、UUID、普通候选引用和展示快照；特殊/纺织机规则仍归各模块。
【线程与所有权】启动或主循环调用。库和文字优先使用PSRAM；find返回的指针只允许同步借用，
不能跨越编辑、加载或语言切换；排队和页面必须复制快照。JSON解析树不长期保留。
*/
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "lang/terminal_lang.h"
#include "sys/sys_audio.h"
#include "sys/sys_command_result.h"
#include "sys/sys_psram_text.h"

struct PrescriptId
{
    uint8_t bytes[16] = {};
    bool empty() const;
    bool operator==(const PrescriptId &other) const;
    bool operator!=(const PrescriptId &other) const { return !(*this == other); }
    bool parse(const char *uuid);
    String string() const;
    static PrescriptId generate();
};

enum : uint8_t
{
    PRESCRIPT_HAS_TITLE = 1 << 0,
    PRESCRIPT_HAS_COLOR = 1 << 1,
    PRESCRIPT_APPLY_TARGET = 1 << 2,
    PRESCRIPT_ALERT = 1 << 3,
    PRESCRIPT_SILENT = 1 << 4,
};

/** 独立内容/拥有所有文字的快照。audio=Invalid表示默认收尾音；静默另用flags表达。 */
struct PrescriptContent
{
    PrescriptId id;
    SysPsramString title;
    SysPsramString text;
    uint16_t color = 0x07FF;
    AudioAssetId audio = AudioAssetId::Invalid;
    uint8_t flags = PRESCRIPT_APPLY_TARGET;
};
#if defined(ARDUINO_ARCH_ESP32)
// ESP32-S3实际ABI：纯属性20字节，标题/正文所有权描述符各8字节；文字缓冲另计。
static_assert(sizeof(PrescriptId) == 16 && sizeof(PrescriptContent) == 36, "指令结构内存布局发生变化");
#endif

namespace SysPrescriptCatalog
{
    /** 启动加载双语（锁定版只加载对应语言）；新库不存在时迁移旧TXT，保留原文件。 */
    void Load();
    const PrescriptContent *Find(SystemLang_t lang, const PrescriptId &id);
    size_t OrdinaryCount(SystemLang_t lang);
    const PrescriptContent *OrdinaryAt(SystemLang_t lang, size_t index);
    bool DrawOrdinary(SystemLang_t lang, PrescriptContent &out);
    /** 拷贝并补全默认标题/颜色；返回false表示文字复制失败，out保持原值。 */
    bool Snapshot(const PrescriptContent &content, SystemLang_t lang, PrescriptContent &out);
    bool Custom(SystemLang_t lang, const char *text, uint16_t color, const char *title, PrescriptContent &out);
    SysOpResult AddOrdinary(SystemLang_t lang, const String &text);
    SysOpResult Update(SystemLang_t lang, const PrescriptContent &content);
    SysOpResult Delete(SystemLang_t lang, const PrescriptId &id);
    /** 只移除普通候选资格，内容及其它模块引用保持；与Delete永久删除内容明确分开。 */
    SysOpResult RemoveOrdinary(SystemLang_t lang, const PrescriptId &id);
    /** 规则损坏/未能完成引用登记时，本次运行禁止删除，保留可读正文供恢复。 */
    void BlockDeletion(SystemLang_t lang);
    /** 规则加载成功后注册外部引用；删除内容时即使未加载对应语言的规则也会保留保护。 */
    bool SetReferences(SystemLang_t lang, const char *module, const SysPsramVector<PrescriptId> &ids);
    bool Ready(SystemLang_t lang);
    // 内容JSON编解码；所有类型/范围错误均拒绝，不静默改成默认值。
    bool Decode(JsonObjectConst object, PrescriptContent &out);
    void Encode(const PrescriptContent &content, JsonObject object);
    /** 有备份的同文件系统JSON提交。启动只恢复已提交正式文件/备份，绝不提升遗留tmp。 */
    bool ReadJson(fs::FS &fs, const char *path, JsonDocument &out);
    bool WriteJson(fs::FS &fs, const char *path, const JsonDocument &document);
    // 展示结果的所有者；不放在特殊抽取引擎，避免纺织机/自定义通知污染剧情结果。
    /** 接收方已持有完整快照时移动所有权，无分配，不会在消费进度后因复制失败丢结果。 */
    void Present(PrescriptContent &&snapshot);
    const PrescriptContent &Presented();
}
