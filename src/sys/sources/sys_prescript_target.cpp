/*
【模块职责】本地使用者服务实现。

本模块只处理数据与规则：
1. 本地使用者 ID 的清洗、新增、删除、选择；
2. 蓝牙同步使用者列表；
3. 指令显示前的“致... / To...”使用者替换。

设备菜单放在 app_prescript_target.cpp；TGT 组蓝牙命令在本文件末尾的【蓝牙命令区】。
这样 UI、协议和数据服务各自独立，后续扩展时不会互相缠住。
*/
#include "sys/sys_prescript_target.h"
#include "sys/sys_config.h"
#include "sys/sys_command_registry.h"
#include "sys/app_manager.h"

namespace {

bool TargetExists(const String &id, int *out_index = nullptr)
{
    for (int i = 0; i < sysConfig.prescript_target_count; i++)
    {
        if (sysConfig.prescript_targets[i] == id)
        {
            if (out_index)
                *out_index = i;
            return true;
        }
    }
    return false;
}


String ApplyChineseTarget(const String &text, const String &target)
{
    auto replaceLeadingTarget = [&](int prefix_len) -> String {
        String rest = text.substring(prefix_len);
        rest.trim();
        if (rest.length() == 0)
            return "致" + target;

        // 旧模板常写成“致... 前往...”或“致...前往...”。替换时统一改成“致使用者，前往...”，
        // 避免使用者名后面留下半角空格。
        if (rest.startsWith("，") || rest.startsWith(",") || rest.startsWith("。") || rest.startsWith("."))
            return "致" + target + rest;
        return "致" + target + "，" + rest;
    };

    // 中文规则：只检查开头，不扫描正文，避免误伤中间出现的“致...”。
    if (text.startsWith("致..."))
        return replaceLeadingTarget(strlen("致..."));

    if (text.startsWith("致…"))
        return replaceLeadingTarget(strlen("致…"));

    return "致" + target + "，" + text;
}

String ApplyEnglishTarget(const String &text, const String &target)
{
    auto replaceLeadingTarget = [&](int prefix_len) -> String {
        String rest = text.substring(prefix_len);
        rest.trim();
        if (rest.length() == 0)
            return "To " + target;

        // 英文模板使用“To... / To…”作为占位头。替换后统一写成“To 使用者, 内容”，
        // 同时兼容模板原本已经带逗号或句点的情况，避免出现双标点。
        if (rest.startsWith(",") || rest.startsWith(".") || rest.startsWith(":") || rest.startsWith(";"))
            return "To " + target + rest;
        return "To " + target + ", " + rest;
    };

    // 英文规则与中文完全分开，避免把“To…”错误补成“致使用者，To…内容”。
    if (text.startsWith("To..."))
        return replaceLeadingTarget(strlen("To..."));

    if (text.startsWith("To…"))
        return replaceLeadingTarget(strlen("To…"));

    if (text.startsWith("To ..."))
        return replaceLeadingTarget(strlen("To ..."));

    if (text.startsWith("To …"))
        return replaceLeadingTarget(strlen("To …"));

    return "To " + target + ", " + text;
}

} // namespace

// 注册 TGT 命令组；定义在文件末尾的【蓝牙命令区】。
static void PrescriptTarget_RegisterCommands();

void SysPrescriptTarget_Init()
{
    PrescriptTarget_RegisterCommands();
}

String SysPrescriptTarget_Sanitize(const String &raw)
{
    String id = raw;
    id.trim();

    // 协议字段使用冒号和竖线分割，ID 本身禁止携带这些字符，避免破坏 BLE 文本协议。
    id.replace(":", "");
    id.replace("|", "");
    id.replace("\r", "");
    id.replace("\n", "");

    if (id.length() > PrescriptConst::MAX_PRESCRIPT_TARGET_LEN)
        id = id.substring(0, PrescriptConst::MAX_PRESCRIPT_TARGET_LEN);

    return id;
}

SysOpResult SysPrescriptTarget_Add(const String &raw_id)
{
    String id = SysPrescriptTarget_Sanitize(raw_id);
    if (id.length() == 0)
    {
        Serial.println("[使用者] 新增失败：使用者 ID 为空。");
        return SysOpResult::Error("EMPTY_ID");
    }

    if (TargetExists(id))
    {
        sysConfig.current_prescript_target = id;
        sysConfig.save();
        Serial.printf("[使用者] 使用者已存在，已切换为当前使用者：%s\n", id.c_str());
        return SysOpResult::Warn("EXISTS_SELECTED", id);
    }

    if (sysConfig.prescript_target_count >= PrescriptConst::MAX_PRESCRIPT_TARGETS)
    {
        Serial.println("[使用者] 新增失败：使用者列表已满。");
        return SysOpResult::Error("FULL");
    }

    sysConfig.prescript_targets[sysConfig.prescript_target_count++] = id;
    sysConfig.current_prescript_target = id;
    sysConfig.save();
    Serial.printf("[使用者] 已新增并选中使用者：%s\n", id.c_str());
    return SysOpResult::Ok("ADDED", id);
}

SysOpResult SysPrescriptTarget_Delete(const String &raw_id)
{
    String id = SysPrescriptTarget_Sanitize(raw_id);
    int index = -1;
    if (id.length() == 0 || !TargetExists(id, &index))
    {
        Serial.printf("[使用者] 删除失败，未找到使用者：%s\n", id.c_str());
        return SysOpResult::Error("NOT_FOUND", id);
    }

    for (int i = index; i < sysConfig.prescript_target_count - 1; i++)
        sysConfig.prescript_targets[i] = sysConfig.prescript_targets[i + 1];
    sysConfig.prescript_target_count--;

    if (sysConfig.current_prescript_target == id)
        sysConfig.current_prescript_target = "";

    sysConfig.save();
    Serial.printf("[使用者] 已删除使用者：%s\n", id.c_str());
    return SysOpResult::Ok("DELETED", id);
}

SysOpResult SysPrescriptTarget_SetCurrent(const String &raw_id)
{
    String id = SysPrescriptTarget_Sanitize(raw_id);
    if (id.length() == 0)
    {
        sysConfig.current_prescript_target = "";
        sysConfig.save();
        Serial.println("[使用者] 已清空当前使用者。");
        return SysOpResult::Ok("CLEARED");
    }

    if (!TargetExists(id))
    {
        Serial.printf("[使用者] 选择失败，未找到使用者：%s\n", id.c_str());
        return SysOpResult::Error("NOT_FOUND", id);
    }

    sysConfig.current_prescript_target = id;
    sysConfig.save();
    Serial.printf("[使用者] 已选中使用者：%s\n", id.c_str());
    return SysOpResult::Ok("SELECTED", id);
}

String SysPrescriptTarget_Apply(const String &raw)
{
    String target = sysConfig.current_prescript_target;
    target.trim();
    if (target.length() == 0)
        return raw;

    String text = raw;
    text.trim();
    if (text.length() == 0)
        return raw;

    if (appManager.getLanguage() == LANG_EN)
        return ApplyEnglishTarget(text, target);

    return ApplyChineseTarget(text, target);
}

// ===== 【蓝牙命令区】TGT 使用者 =====
// 处理函数只把参数交给使用者业务入口；修改后不再主动回传列表，客户端按需发 TGT.LIST。

static const SysCmdArgSpec kTgtIdArgs[] = {
    SYS_ARG_REST("id", 1, PrescriptConst::MAX_PRESCRIPT_TARGET_LEN, ARG_TRIM),
};

static const SysCmdArgSpec kTgtSetArgs[] = {
    SYS_ARG_REST("id", 0, PrescriptConst::MAX_PRESCRIPT_TARGET_LEN, ARG_TRIM),
};

static void Cmd_TgtAdd(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(SysPrescriptTarget_Add(args.string(0)));
}

static void Cmd_TgtDel(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(SysPrescriptTarget_Delete(args.string(0)));
}

// TGT.SET：空 ID 表示清除当前使用者。
static void Cmd_TgtSet(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(SysPrescriptTarget_SetCurrent(args.string(0)));
}

// TGT.LIST：只有一条记录 → {"current","items":[...]}。
static bool Stream_TgtList(uint16_t index, JsonDocument &out)
{
    if (index > 0)
        return false;
    out["current"] = sysConfig.current_prescript_target;
    JsonArray items = out["items"].to<JsonArray>();
    for (int i = 0; i < sysConfig.prescript_target_count; i++)
        items.add(sysConfig.prescript_targets[i]);
    return true;
}

static const SysCmdSpec kTgtCmds[] = {
    {"LIST", nullptr, 0, CMD_SYNC, nullptr, Stream_TgtList},
    {"ADD", kTgtIdArgs, SYS_COUNT_OF(kTgtIdArgs), 0, Cmd_TgtAdd, nullptr},
    {"DEL", kTgtIdArgs, SYS_COUNT_OF(kTgtIdArgs), 0, Cmd_TgtDel, nullptr},
    {"SET", kTgtSetArgs, SYS_COUNT_OF(kTgtSetArgs), 0, Cmd_TgtSet, nullptr},
};

static const SysCmdGroup kTgtGroup = {"TGT", 1, SRC_ALL, kTgtCmds, SYS_COUNT_OF(kTgtCmds)};

static void PrescriptTarget_RegisterCommands()
{
    SysCommand_RegisterGroup(&kTgtGroup);
}

// ===== 【蓝牙命令区结束】 =====
