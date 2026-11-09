/*
【模块职责】各指令库共用的内容格式、UUID、属性准备和JSON文件提交；不持有任何全局内容池。
【线程与所有权】启动或主循环调用。库和文字优先使用PSRAM；find返回的指针只允许同步借用，
不能跨越编辑、加载或语言切换；排队和页面必须复制快照。JSON解析树不长期保留。
*/
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
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
};

/** 独立内容/拥有所有文字的快照。audio=Invalid表示默认收尾音。 */
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

/**
 * 仅同步借用文字的内容视图；不拥有指针，不进入队列、不写入文件。
 * title=nullptr表示缺省，title=""表示明确空标题；color默认青色，显式黑色有效。
 * flags只控制收件人替换和手动警报，标题/颜色是否缺省在转换已有内容时统一处理。
 * 可从已有内容借用，也可直接借用临时通知的正文；调用Prepare/Submit完成后即可释放输入。
 */
struct PrescriptContentView
{
    PrescriptId id;
    const char *text = nullptr;
    const char *title = nullptr;
    uint16_t color = 0x07FF;
    AudioAssetId audio = AudioAssetId::Invalid;
    uint8_t flags = PRESCRIPT_APPLY_TARGET;

    PrescriptContentView() = default;
    PrescriptContentView(const char *body) : text(body) {}
    PrescriptContentView(const PrescriptContent &content)
        : id(content.id), text(content.text.c_str()),
          title(content.flags & PRESCRIPT_HAS_TITLE ? content.title.c_str() : nullptr),
          color(content.flags & PRESCRIPT_HAS_COLOR ? content.color : 0x07FF),
          audio(content.audio), flags(content.flags & (PRESCRIPT_APPLY_TARGET | PRESCRIPT_ALERT)) {}
};

/** 每个库独立拥有内容和版本；语言/类型由文件与调用者校验，不给每条指令增加归属字段。 */
struct PrescriptLibrary {
    PrescriptId id;
    uint32_t revision = 0;
    SysPsramVector<PrescriptContent> contents;
};
namespace SysPrescript {
    /** 校验并复制公共属性，失败保持out不变；空指针正文拒绝，临时空字符串正文兼容有效。 */
    SysOpResult Prepare(const PrescriptContentView &content, SystemLang_t lang, PrescriptContent &out);
    bool Decode(JsonObjectConst object, PrescriptContent &out);
    void Encode(const PrescriptContent &content, JsonObject object);
    /** 借用仅在本库下一次编辑/重载前有效；跨库查询必须由调用方显式选择库。 */
    const PrescriptContent *Find(const PrescriptLibrary &library, const PrescriptId &id);
    /** schema=2完整库元数据/独立内容校验；不解释任何特殊链条或纺织机规则。 */
    bool DecodeLibrary(const JsonDocument &document, SystemLang_t lang, const char *kind, PrescriptLibrary &out);
    void EncodeLibrary(const PrescriptLibrary &library, SystemLang_t lang, const char *kind, JsonDocument &out);
    /** 可恢复提交：只恢复正式文件/备份，不提升未提交tmp；主循环串行调用。 */
    bool ReadJson(fs::FS &fs, const char *path, JsonDocument &out);
    bool WriteJson(fs::FS &fs, const char *path, const JsonDocument &document);
    /** 只读迁移源。仅目标库缺失/规则为已知schema1时使用，不删除旧LittleFS数据。 */
    bool ReadLegacyCatalog(SystemLang_t lang, JsonDocument &out);
    /** 按明确的旧内容ID集合构造自包含库，保留内容UUID；调用方验证完整业务规则后才能落盘。 */
    bool MigrateLibrary(SystemLang_t lang, const char *kind, JsonArrayConst ids, JsonDocument &document);
}
