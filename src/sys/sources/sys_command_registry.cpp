/*
【模块职责】蓝牙指令框架实现（协议 v2）：组表、参数校验、回包编码、列表流调度、串口调试来源和 SYS 组。
【数据流】命令行队列 → SysCommand_Update（主循环）→ 解析请求头 → 查组/查命令 → 来源与语言锁检查
→ 拆分并校验参数 → 处理函数或列表流 → ACK / DAT 行写入蓝牙发送缓冲（串口来源则打印到串口）。
【重要约束】
- 请求在同一轮主循环冻结回包快照；之后只分批发送已冻结的字节，界面/日历修改不会改变本次结果。
- 一个请求完成发送前不执行下一请求；拥塞时保留 DAT/ACK，逐轮重试，不重跑业务处理函数。
- NFC 来源没有回传通道，回包只写串口日志摘要。
*/
#include "sys/sys_command_registry.h"
#include "sys/sys_ble.h"
#include "sys/sys_protocol.h"
#include "sys/sys_psram_json.h"
#include "sys/app_manager.h"
#include "hal/hal.h"
#include "lang/terminal_lang.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>

namespace
{
    constexpr uint8_t kProtocolVersion = 2;
    // 每轮主循环最多交给发送环的 DAT 数量；快照采集另受字节数和记录数上限约束。
    constexpr uint8_t kStreamRecordsPerLoop = 4;

    const SysCmdGroup *s_groups[PrescriptConst::MAX_CMD_GROUPS] = {};
    volatile uint8_t s_group_count = 0;

    // 只在主循环使用的大缓冲放在静态区，避免占用主循环栈。
    SysCmdLine s_line;
    SysCmdArgs s_args;
    SysCmdLineAssembler s_serial_assembler(SysCmdSource::Serial);

    const char *LevelText(SysCmdResultLevel level)
    {
        switch (level)
        {
        case SysCmdResultLevel::Warn:
            return "WARN";
        case SysCmdResultLevel::Error:
            return "ERR";
        case SysCmdResultLevel::Ok:
        default:
            return "OK";
        }
    }

    /**
     * 一次请求的回包作业。DAT 字节优先放入 PSRAM，ACK 使用固定数组，内存不足时仍能回错误。
     * 只在主循环使用；已执行的业务不会因发送重试再次运行，连接代次变化则释放整份旧作业。
     */
    struct ReplyJob
    {
        bool active = false;
        SysCmdSource source = SysCmdSource::Ble;
        uint32_t session = 0;
        char *bytes = nullptr;
        size_t capacity = 0;
        size_t used = 0;
        size_t sent = 0;
        uint32_t records = 0;
        const char *error = nullptr;
        char ack[PrescriptConst::CMD_LINE_MAX + 128] = {};
        bool has_ack = false;
    };
    ReplyJob s_reply;

    void ClearReplyData()
    {
        SysPsramJsonAllocator::Instance()->deallocate(s_reply.bytes);
        s_reply.bytes = nullptr;
        s_reply.capacity = s_reply.used = s_reply.sent = 0;
        s_reply.records = 0;
    }

    void ResetReply()
    {
        ClearReplyData();
        s_reply.active = false;
        s_reply.has_ack = false;
        s_reply.error = nullptr;
    }

    void BeginReply(SysCmdSource source, uint32_t session)
    {
        ResetReply();
        s_reply.active = true;
        s_reply.source = source;
        s_reply.session = session;
    }

    // 失败后丢弃整份未发送的快照，由最终 ACK 明确报错，不能以成功 END 交付一份残缺列表。
    bool FailReply(const char *code)
    {
        ClearReplyData();
        s_reply.error = code;
        return false;
    }

    bool ReserveReply(size_t needed)
    {
        if (needed <= s_reply.capacity)
            return true;
        size_t capacity = s_reply.capacity ? s_reply.capacity : 1024;
        while (capacity < needed)
            capacity *= 2;
        if (capacity > PrescriptConst::CMD_REPLY_MAX_BYTES + 1)
            capacity = PrescriptConst::CMD_REPLY_MAX_BYTES + 1;
        void *bytes = SysPsramJsonAllocator::Instance()->reallocate(s_reply.bytes, capacity);
        if (bytes == nullptr)
            return FailReply("OUT_OF_MEMORY");
        s_reply.bytes = static_cast<char *>(bytes);
        s_reply.capacity = capacity;
        return true;
    }

    /** 原子写一整行；false 表示暂时未交给传输层，作业必须保留该行重试。 */
    bool EmitLine(SysCmdSource source, const char *line, size_t length, uint32_t session)
    {
        switch (source)
        {
        case SysCmdSource::Ble:
            return SysBLE_Write(line, length, session);
        case SysCmdSource::Serial:
            Serial.print("[蓝牙回包] ");
            Serial.print(line);
            return true;
        case SysCmdSource::Nfc:
        default:
            if (strncmp(line, "ACK", 3) == 0)
                Serial.printf("[NFC] 卡片指令执行结果：%s", line);
            return true;
        }
    }

    /** ACK 先保存在固定区，只有成功交给来源后才释放请求，不受发送环暂时满的影响。 */
    void EmitAck(SysCmdSource, const char *id, const char *name, const SysOpResult &result)
    {
        const bool failed = s_reply.error != nullptr;
        const char *level = failed ? "ERR" : LevelText(result.level);
        const char *code = failed ? s_reply.error : (result.code ? result.code : "OK");
        const char *detail = failed ? "" : result.detail.c_str();
        const int length = snprintf(s_reply.ack, sizeof(s_reply.ack), "ACK%s%s:%s:%s:%s%s%s\n",
                                    id && id[0] ? "#" : "", id ? id : "", name, level, code,
                                    detail[0] ? ":" : "", detail);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(s_reply.ack))
        {
            FailReply("REPLY_TOO_LARGE");
            snprintf(s_reply.ack, sizeof(s_reply.ack), "ACK%s%s:%s:ERR:REPLY_TOO_LARGE\n",
                     id && id[0] ? "#" : "", id ? id : "", name);
        }
        // ACK 细节允许冒号，但不能带真实换行或回车；保留最后的行结束符。
        for (size_t i = 0; s_reply.ack[i] && s_reply.ack[i + 1]; ++i)
            if (s_reply.ack[i] == '\n' || s_reply.ack[i] == '\r')
                s_reply.ack[i] = ' ';
        s_reply.has_ack = true;
    }

    /**
     * 把 DAT 编码进请求快照，不直接发 BLE。提前检查单行和快照上限，永久错误不会反复重试。
     * JSON 直接序列化到 PSRAM 字节区，避免为每条正文额外分配两个临时 String。
     */
    bool EmitData(SysCmdSource source, const char *id, const char *name, const JsonDocument &document)
    {
        if (s_reply.error)
            return false;
        if (document.overflowed())
            return FailReply("OUT_OF_MEMORY");
        char prefix[64];
        const int prefix_length = snprintf(prefix, sizeof(prefix), "DAT%s%s:%s:",
                                           id && id[0] ? "#" : "", id ? id : "", name);
        const size_t json_length = measureJson(document);
        const size_t length = static_cast<size_t>(prefix_length) + json_length + 1;
        size_t line_limit = PrescriptConst::BLE_TX_BUFFER_BYTES;
        if (source == SysCmdSource::Ble && SysBLE_TxCapacity() < line_limit)
            line_limit = SysBLE_TxCapacity();
        if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= sizeof(prefix) || length > line_limit)
            return FailReply("REPLY_TOO_LARGE");
        if (s_reply.records >= PrescriptConst::CMD_REPLY_MAX_RECORDS ||
            length > PrescriptConst::CMD_REPLY_MAX_BYTES - s_reply.used)
            return FailReply("SNAPSHOT_TOO_LARGE");
        if (!ReserveReply(s_reply.used + length + 1))
            return false;
        char *line = s_reply.bytes + s_reply.used;
        memcpy(line, prefix, static_cast<size_t>(prefix_length));
        const size_t written = serializeJson(document, line + prefix_length, json_length + 1);
        if (written != json_length)
            return FailReply("OUT_OF_MEMORY");
        line[length - 1] = '\n';
        line[length] = '\0';
        s_reply.used += length;
        ++s_reply.records;
        return true;
    }

    /** 每轮至多交付四条 DAT；ACK 必须排在最后。发送失败只暂停，绝不重跑 Handler。 */
    void UpdateReply()
    {
        if (!s_reply.active)
            return;
        if (s_reply.source == SysCmdSource::Ble &&
            (!SysBLE_IsLinkReady() || s_reply.session != SysBLE_GetSession()))
        {
            ResetReply();
            return;
        }
        for (uint8_t step = 0; step < kStreamRecordsPerLoop && s_reply.sent < s_reply.used; ++step)
        {
            char *line = s_reply.bytes + s_reply.sent;
            const char *end = static_cast<const char *>(memchr(line, '\n', s_reply.used - s_reply.sent));
            const size_t length = static_cast<size_t>(end - line) + 1;
            // 串口 print 需要以零结尾，暂时替换下一行首字节，发送后恢复。
            const char saved = line[length];
            line[length] = '\0';
            const bool emitted = EmitLine(s_reply.source, line, length, s_reply.session);
            line[length] = saved;
            if (!emitted)
                return;
            s_reply.sent += length;
        }
        if (s_reply.sent == s_reply.used && s_reply.has_ack &&
            EmitLine(s_reply.source, s_reply.ack, strlen(s_reply.ack), s_reply.session))
            ResetReply();
    }

    // ---------- 同步闪框（非阻塞）----------
    // 原实现在命令里 delay 循环约 200ms；现在每 15ms 画一帧，由 SysCommand_Update 推进。
    // 只画屏幕边框，不覆盖当前页面；同一帧内由外到内逐层降低亮度。
    constexpr uint8_t kFlashBorderLayers = 4;

    uint16_t FlashCyan(uint8_t intensity)
    {
        const uint16_t g = (static_cast<uint16_t>(intensity) * 63U) / 255U;
        return static_cast<uint16_t>((g << 5) | b) const uint16_t b = (static_cast<uint16_t>(intensity) * 31U) / 255U;
        ;
    }

    int s_flash_intensity = -1;
    uint32_t s_flash_last_ms = 0;

    void StartSyncFlash()
    {
        s_flash_intensity = 255;
        s_flash_last_ms = 0;
    }

    void UpdateSyncFlash()
    {
        if (s_flash_intensity < 0 || millis() - s_flash_last_ms < 15)
            return;
        s_flash_last_ms = millis();
        const int width = HAL_Get_Screen_Width();
        const int height = HAL_Get_Screen_Height();
        for (uint8_t layer = 0; layer < kFlashBorderLayers; ++layer)
        {
            // 外圈保持最亮，向内每层衰减；同时随整帧强度一起 fade。
            const uint8_t layer_intensity = static_cast<uint8_t>(
                (s_flash_intensity * (kFlashBorderLayers - layer)) /
                kFlashBorderLayers);
            const uint16_t color = FlashCyan(layer_intensity);
            const int inset = layer;
            HAL_Draw_Rect(inset, inset, width - inset * 2, height - inset * 2, color);
        }
        HAL_Screen_Update();
        s_flash_intensity -= 20;
    }

    // ---------- 列表快照 ----------
    const SysCmdSpec *FindSyncSpec(const SysCmdGroup *group)
    {
        for (uint8_t i = 0; i < group->cmd_count; ++i)
            if ((group->cmds[i].flags & CMD_SYNC) && group->cmds[i].stream)
                return &group->cmds[i];
        return nullptr;
    }

    /**
     * 在当前主循环连续读取列表，不让 UI、日历或语言切换夹在两条记录之间。
     * 每条立即编码到有界快照；快照建立不等待链路、不写文件，达到容量/记录上限立即失败。
     * 回调必须只读内存、无 I/O；发送仍由 UpdateReply 跨帧推进，快照内存发送结束或断开即释放。
     */
    bool CaptureGroup(SysCmdSource source, const char *id, const SysCmdGroup *group, const SysCmdSpec *spec)
    {
        char name[28];
        snprintf(name, sizeof(name), "%s.%s", group->code, spec->verb);
        for (uint32_t index = 0; index <= PrescriptConst::CMD_REPLY_MAX_RECORDS; ++index)
        {
            JsonDocument document(SysPsramJsonAllocator::Instance());
            const bool found = spec->stream(static_cast<uint16_t>(index), document);
            if (document.overflowed())
                return FailReply("OUT_OF_MEMORY");
            if (!found)
                return true;
            if (!EmitData(source, id, name, document))
                return false;
        }
        return FailReply("SNAPSHOT_TOO_LARGE");
    }

    void StartStream(SysCmdSource source, const char *id, const char *ack_name,
                     const SysCmdGroup *group, const SysCmdSpec *spec, bool sync_all)
    {
        if (sync_all)
        {
            for (uint8_t i = 0; i < s_group_count; ++i)
            {
                const SysCmdGroup *next = s_groups[i];
                const SysCmdSpec *list = FindSyncSpec(next);
                if (list && (next->sources & static_cast<uint8_t>(source)) &&
                    !CaptureGroup(source, id, next, list))
                    break;
            }
        }
        else
            CaptureGroup(source, id, group, spec);
        EmitAck(source, id, ack_name, SysOpResult::Ok("END", String(s_reply.records)));
    }

    // ---------- 参数校验 ----------

    // 严格十进制整数：可带一个负号，不允许空串、空格和其他字符。
    bool ParseInt(const char *text, int32_t &out)
    {
        if (text == nullptr || *text == '\0')
            return false;
        const char *p = text;
        bool negative = false;
        if (*p == '-')
        {
            negative = true;
            ++p;
        }
        if (*p == '\0')
            return false;
        int64_t value = 0;
        for (; *p; ++p)
        {
            if (*p < '0' || *p > '9')
                return false;
            value = value * 10 + (*p - '0');
            if (value > INT32_MAX)
                return false;
        }
        out = static_cast<int32_t>(negative ? -value : value);
        return true;
    }

    int MatchChoice(const char *choices, const char *value)
    {
        int index = 0;
        const char *start = choices;
        while (start && *start)
        {
            const char *bar = strchr(start, '|');
            const size_t length = bar ? static_cast<size_t>(bar - start) : strlen(start);
            if (strlen(value) == length && strncmp(start, value, length) == 0)
                return index;
            ++index;
            start = bar ? bar + 1 : nullptr;
        }
        return -1;
    }

    // 就地去掉字段首尾空白（字段位于 SysCmdArgs::buffer_ 内，可修改）。
    void TrimField(const char *&field, uint16_t &length)
    {
        char *begin = const_cast<char *>(field);
        while (length > 0 && isspace(static_cast<unsigned char>(*begin)))
        {
            ++begin;
            --length;
        }
        while (length > 0 && isspace(static_cast<unsigned char>(begin[length - 1])))
            --length;
        begin[length] = '\0';
        field = begin;
    }
}

/**
 * 框架内部操作的集合；声明为 SysCmdArgs / SysCmdCtx 的友元，用来填写它们的私有字段。
 */
struct SysCommandFramework
{
    /** 拆分并校验参数；失败时直接回 ACK 错误并返回 false。 */
    static bool PrepareArgs(SysCmdCtx &ctx, const SysCmdSpec &spec, const SysCmdHead &head)
    {
        SysCmdArgs &args = s_args;
        args.count_ = 0;
        args.present_ = 0;

        const bool rest_last = spec.arg_count > 0 && spec.args[spec.arg_count - 1].type == SysCmdArgType::Rest;
        int count = 0;
        if (spec.arg_count == 0)
        {
            if (head.args != nullptr && head.args_length > 0)
            {
                ctx.error("ARG_COUNT");
                return false;
            }
        }
        else
        {
            count = SysProtocol_SplitArgs(head.args, head.args_length, spec.arg_count, rest_last,
                                          args.buffer_, sizeof(args.buffer_), args.fields_, args.lengths_);
            if (count == SYS_PROTOCOL_ARGS_TOO_MANY)
            {
                ctx.error("ARG_COUNT");
                return false;
            }
            if (count == SYS_PROTOCOL_ARGS_NO_SPACE)
            {
                ctx.error("TOO_LONG");
                return false;
            }
        }

        args.count_ = spec.arg_count;
        for (uint8_t i = 0; i < spec.arg_count; ++i)
        {
            const SysCmdArgSpec &arg = spec.args[i];
            const bool optional = (arg.flags & ARG_OPTIONAL) != 0;
            if (i >= count)
            {
                if (optional)
                    continue;
                ctx.error("ARG_MISSING", arg.name);
                return false;
            }
            if (arg.flags & ARG_TRIM)
                TrimField(args.fields_[i], args.lengths_[i]);
            if (optional && args.lengths_[i] == 0)
                continue;

            switch (arg.type)
            {
            case SysCmdArgType::Int:
                if (!ParseInt(args.fields_[i], args.numbers_[i]))
                {
                    ctx.error(args.lengths_[i] == 0 ? "ARG_MISSING" : "ARG_FORMAT", arg.name);
                    return false;
                }
                if (args.numbers_[i] < arg.min || args.numbers_[i] > arg.max)
                {
                    ctx.error("ARG_RANGE", arg.name);
                    return false;
                }
                break;
            case SysCmdArgType::Choice:
                args.numbers_[i] = MatchChoice(arg.choices, args.fields_[i]);
                if (args.numbers_[i] < 0)
                {
                    ctx.error("ARG_RANGE", arg.name);
                    return false;
                }
                break;
            case SysCmdArgType::Str:
            case SysCmdArgType::Rest:
            default:
                if (static_cast<int32_t>(args.lengths_[i]) < arg.min)
                {
                    ctx.error(args.lengths_[i] == 0 ? "ARG_MISSING" : "ARG_LENGTH", arg.name);
                    return false;
                }
                if (arg.max > 0 && static_cast<int32_t>(args.lengths_[i]) > arg.max)
                {
                    ctx.error("ARG_LENGTH", arg.name);
                    return false;
                }
                break;
            }
            args.present_ |= static_cast<uint16_t>(1u << i);
        }
        return true;
    }

    static void InitCtx(SysCmdCtx &ctx, SysCmdSource source, const char *id, const char *name)
    {
        ctx.source_ = source;
        snprintf(ctx.id_, sizeof(ctx.id_), "%s", id ? id : "");
        snprintf(ctx.name_, sizeof(ctx.name_), "%s", name ? name : "?");
        ctx.acked_ = false;
    }

    static void Execute(const SysCmdLine &line)
    {
        SysCmdCtx ctx;
        SysCmdHead head;
        if (!SysProtocol_ParseHead(line.text, line.length, head))
        {
            char name[28];
            char id[9];
            SysProtocol_ExtractReplyTarget(line.text, name, sizeof(name), id, sizeof(id));
            InitCtx(ctx, line.source, id, name);
            ctx.error("BAD_REQUEST");
            return;
        }

        char name[28];
        snprintf(name, sizeof(name), "%s.%s", head.group, head.verb);
        InitCtx(ctx, line.source, head.id, name);

        const SysCmdGroup *group = nullptr;
        for (uint8_t i = 0; i < s_group_count; ++i)
            if (strcmp(s_groups[i]->code, head.group) == 0)
                group = s_groups[i];
        if (group == nullptr)
        {
            ctx.error("UNKNOWN_GROUP");
            return;
        }
        if ((group->sources & static_cast<uint8_t>(line.source)) == 0)
        {
            Serial.printf("[指令框架] 来源 %u 不允许调用 %s，已拒绝。\n",
                          static_cast<unsigned>(line.source), name);
            ctx.error("FORBIDDEN_SOURCE");
            return;
        }

        const SysCmdSpec *spec = nullptr;
        for (uint8_t i = 0; i < group->cmd_count; ++i)
            if (strcmp(group->cmds[i].verb, head.verb) == 0)
                spec = &group->cmds[i];
        if (spec == nullptr)
        {
            ctx.error("UNKNOWN_VERB");
            return;
        }

        if (!PrepareArgs(ctx, *spec, head))
            return;

        if (spec->flags & CMD_LANG_LOCKED)
        {
            const SystemLang_t lang = s_args.choice(0) == 0 ? LANG_ZH : LANG_EN;
            if (!TerminalLang::Accepts(lang))
            {
                ctx.error("LANG_LOCKED", TerminalLang::BUILD_CODE);
                return;
            }
        }

        if (spec->stream)
        {
            StartStream(line.source, head.id, name, group, spec, false);
            return;
        }
        if (spec->handler)
            spec->handler(ctx, s_args);
        if (!ctx.acked())
            ctx.ok("DONE");
    }

    /** SYS.SYNC：带范围时只同步该组，不带时遍历全部同步组；最终 ACK 由列表流在结束时发送。 */
    static void SysSync(SysCmdCtx &ctx, const SysCmdArgs &args)
    {
        if (args.has(0))
        {
            for (uint8_t i = 0; i < s_group_count; ++i)
            {
                if (strcmp(s_groups[i]->code, args.str(0)) != 0)
                    continue;
                if ((s_groups[i]->sources & static_cast<uint8_t>(ctx.source_)) == 0)
                {
                    ctx.error("FORBIDDEN_SOURCE");
                    return;
                }
                const SysCmdSpec *spec = FindSyncSpec(s_groups[i]);
                if (spec == nullptr)
                    break;
                Serial.printf("[蓝牙同步] 开始同步：%s。\n", s_groups[i]->code);
                StartSyncFlash();
                StartStream(ctx.source_, ctx.id_, ctx.name_, s_groups[i], spec, false);
                ctx.acked_ = true;
                return;
            }
            ctx.error("UNKNOWN_GROUP", args.string(0));
            return;
        }
        Serial.println("[蓝牙同步] 开始全量同步。");
        StartSyncFlash();
        StartStream(ctx.source_, ctx.id_, ctx.name_, nullptr, nullptr, true);
        ctx.acked_ = true;
    }
};

void SysCmdCtx::ack(const SysOpResult &result)
{
    if (acked_)
        return;
    acked_ = true;
    EmitAck(source_, id_, name_, result);
}

bool SysCmdCtx::data(const JsonDocument &document)
{
    if (acked_)
        return false;
    return EmitData(source_, id_, name_, document);
}

// ===== 【蓝牙命令区】SYS 系统 =====

// SYS.INFO：协议版本、语言状态和已注册的组及其版本。
static void Cmd_SysInfo(SysCmdCtx &ctx, const SysCmdArgs &)
{
    JsonDocument document(SysPsramJsonAllocator::Instance());
    document["proto"] = kProtocolVersion;
    document["lang"] = TerminalLang::Code(appManager.getLanguage());
    document["lang_mode"] = TerminalLang::LOCKED ? "LOCKED" : "RUNTIME";
    document["build_lang"] = TerminalLang::BUILD_CODE;
    JsonArray groups = document["groups"].to<JsonArray>();
    for (uint8_t i = 0; i < s_group_count; ++i)
    {
        JsonObject item = groups.add<JsonObject>();
        item["code"] = s_groups[i]->code;
        item["ver"] = s_groups[i]->version;
    }
    ctx.data(document);
    ctx.ok("OK");
}

// SYS.SYNC[:组码]：逐组输出列表流，最后一条 ACK 的细节是总条数。
// 需要读取请求编号并把最终 ACK 交给列表流发送，所以实现放在框架友元里。
static void Cmd_SysSync(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    SysCommandFramework::SysSync(ctx, args);
}

static const SysCmdArgSpec kSysSyncArgs[] = {
    SYS_ARG_STR("scope", 0, 11, ARG_TRIM | ARG_OPTIONAL),
};

static const SysCmdSpec kSysCmds[] = {
    {"INFO", nullptr, 0, 0, Cmd_SysInfo, nullptr},
    {"SYNC", kSysSyncArgs, SYS_COUNT_OF(kSysSyncArgs), 0, Cmd_SysSync, nullptr},
};

static const SysCmdGroup kSysGroup = {"SYS", 1, SRC_ALL, kSysCmds, SYS_COUNT_OF(kSysCmds)};

// ===== 【蓝牙命令区结束】 =====

void SysCommand_Init()
{
    SysCommand_RegisterGroup(&kSysGroup);
}

bool SysCommand_RegisterGroup(const SysCmdGroup *group)
{
    if (group == nullptr || group->code == nullptr || group->cmds == nullptr || group->cmd_count == 0)
    {
        Serial.println("[指令框架] 注册失败：命令组结构不完整。");
        return false;
    }
    for (uint8_t i = 0; i < s_group_count; ++i)
    {
        if (strcmp(s_groups[i]->code, group->code) == 0)
        {
            Serial.printf("[指令框架] 注册失败：组码 %s 已存在。\n", group->code);
            return false;
        }
    }
    if (s_group_count >= PrescriptConst::MAX_CMD_GROUPS)
    {
        Serial.printf("[指令框架] 注册失败：组数已达上限，%s 未注册。\n", group->code);
        return false;
    }
    for (uint8_t i = 0; i < group->cmd_count; ++i)
    {
        const SysCmdSpec &spec = group->cmds[i];
        if (spec.arg_count > PrescriptConst::MAX_CMD_ARGS || (spec.handler == nullptr) == (spec.stream == nullptr))
        {
            Serial.printf("[指令框架] 注册失败：%s.%s 的参数数量或处理函数不合法。\n", group->code, spec.verb);
            return false;
        }
    }
    // 先写指针再增加计数，NFC 任务并发读取时只会看到完整的条目。
    s_groups[s_group_count] = group;
    s_group_count = s_group_count + 1;
    Serial.printf("[指令框架] 已注册命令组 %s（%u 条命令）。\n", group->code, static_cast<unsigned>(group->cmd_count));
    return true;
}

void SysCommand_Update()
{
    // 1. 串口调试来源：把已到达的字节拼成行放入同一个命令队列。
    uint8_t serial_bytes[64];
    int available = Serial.available();
    while (available > 0)
    {
        const int chunk = available < static_cast<int>(sizeof(serial_bytes)) ? available : static_cast<int>(sizeof(serial_bytes));
        const int read = Serial.readBytes(serial_bytes, chunk);
        if (read <= 0)
            break;
        s_serial_assembler.feed(serial_bytes, static_cast<size_t>(read));
        available -= read;
    }

    UpdateSyncFlash();
    if (s_reply.active)
    {
        UpdateReply();
        return; // 本轮已推进回包，即使刚发完也留到下一轮再执行新请求，保持每轮 DAT 上限。
    }

    // 拒收通知也要等 ACK 入发送环后才取下一条，不能因拥塞从通知队列取出后丢失。
    SysCmdRejectNotice notice;
    if (SysCmdQueue_PopReject(notice))
    {
        if (notice.source == SysCmdSource::Ble &&
            (!SysBLE_IsLinkReady() || notice.session != SysBLE_GetSession()))
            return;
        BeginReply(notice.source, notice.session);
        char name[28];
        char id[9];
        SysProtocol_ExtractReplyTarget(notice.head, name, sizeof(name), id, sizeof(id));
        EmitAck(notice.source, id, name,
                SysOpResult::Error(notice.reason == SysCmdReject::Busy ? "BUSY" : "TOO_LONG"));
        UpdateReply();
        return;
    }
    if (SysCmdQueue_Pop(s_line))
    {
        if (s_line.source == SysCmdSource::Ble &&
            (!SysBLE_IsLinkReady() || s_line.session != SysBLE_GetSession()))
            return;
        BeginReply(s_line.source, s_line.session);
        SysCommandFramework::Execute(s_line);
        UpdateReply();
    }
}

void SysCommand_Event(const char *name, const JsonDocument &payload)
{
    if (name == nullptr || !SysBLE_IsLinkReady())
        return;
    String json;
    serializeJson(payload, json);
    String line = "EVT:";
    line += name;
    line += ':';
    line += json;
    line += '\n';
    EmitLine(SysCmdSource::Ble, line.c_str(), line.length(), SysBLE_GetSession());
}

int SysCommand_FindLineStart(const char *text, size_t length)
{
    if (text == nullptr)
        return -1;
    int earliest = -1;
    const uint8_t count = s_group_count;
    for (uint8_t g = 0; g < count; ++g)
    {
        const char *code = s_groups[g]->code;
        const size_t code_length = strlen(code);
        for (size_t i = 0; i + code_length < length; ++i)
        {
            if (earliest >= 0 && static_cast<int>(i) >= earliest)
                break;
            if (text[i + code_length] == '.' && strncmp(text + i, code, code_length) == 0)
            {
                earliest = static_cast<int>(i);
                break;
            }
        }
    }
    return earliest;
}
