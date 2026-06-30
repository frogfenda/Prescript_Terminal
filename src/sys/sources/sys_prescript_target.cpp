/*
【模块职责】本地使用者服务实现。

本模块只处理数据与规则：
1. 本地使用者 ID 的清洗、新增、删除、选择；
2. 蓝牙同步使用者列表；
3. 指令显示前的“致... / To...”使用者替换。

设备菜单放在 app_prescript_target.cpp，协议解析放在 sys_protocol/sys_router。
这样 UI、协议和数据服务各自独立，后续扩展时不会互相缠住。
*/
#include "sys/sys_prescript_target.h"
#include "sys/sys_config.h"
#include "sys/sys_ble.h"
#include "sys/sys_command_result.h"
#include "sys/sys_event.h"
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

String EscapeJson(String value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    return value;
}

void SyncTargetList(void *payload)
{
    if (!SysEvent_BleSyncScopeMatches(payload, "TGT"))
        return;
    SysPrescriptTarget_SyncBLE();
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

void SysPrescriptTarget_Init()
{
    SysEvent_Subscribe(EVT_BLE_SYNC_REQ, SyncTargetList);
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

bool SysPrescriptTarget_Add(const String &raw_id, String *out_code)
{
    String id = SysPrescriptTarget_Sanitize(raw_id);
    if (id.length() == 0)
    {
        if (out_code)
            *out_code = "EMPTY_ID";
        SysCmdResult_Error("EMPTY_ID");
        Serial.println("[使用者] 新增失败：使用者 ID 为空。");
        return false;
    }

    if (TargetExists(id))
    {
        sysConfig.current_prescript_target = id;
        sysConfig.save();
        if (out_code)
            *out_code = "EXISTS_SELECTED";
        SysCmdResult_Warn("EXISTS_SELECTED", id);
        Serial.printf("[使用者] 使用者已存在，已切换为当前使用者：%s\n", id.c_str());
        return true;
    }

    if (sysConfig.prescript_target_count >= PrescriptConst::MAX_PRESCRIPT_TARGETS)
    {
        if (out_code)
            *out_code = "FULL";
        SysCmdResult_Error("FULL");
        Serial.println("[使用者] 新增失败：使用者列表已满。");
        return false;
    }

    sysConfig.prescript_targets[sysConfig.prescript_target_count++] = id;
    sysConfig.current_prescript_target = id;
    sysConfig.save();

    if (out_code)
        *out_code = "ADDED";
    SysCmdResult_Ok("ADDED", id);
    Serial.printf("[使用者] 已新增并选中使用者：%s\n", id.c_str());
    return true;
}

bool SysPrescriptTarget_Delete(const String &raw_id, String *out_code)
{
    String id = SysPrescriptTarget_Sanitize(raw_id);
    int index = -1;
    if (id.length() == 0 || !TargetExists(id, &index))
    {
        if (out_code)
            *out_code = "NOT_FOUND";
        SysCmdResult_Error("NOT_FOUND", id);
        Serial.printf("[使用者] 删除失败，未找到使用者：%s\n", id.c_str());
        return false;
    }

    for (int i = index; i < sysConfig.prescript_target_count - 1; i++)
        sysConfig.prescript_targets[i] = sysConfig.prescript_targets[i + 1];
    sysConfig.prescript_target_count--;

    if (sysConfig.current_prescript_target == id)
        sysConfig.current_prescript_target = "";

    sysConfig.save();

    if (out_code)
        *out_code = "DELETED";
    SysCmdResult_Ok("DELETED", id);
    Serial.printf("[使用者] 已删除使用者：%s\n", id.c_str());
    return true;
}

bool SysPrescriptTarget_SetCurrent(const String &raw_id, String *out_code)
{
    String id = SysPrescriptTarget_Sanitize(raw_id);
    if (id.length() == 0)
    {
        sysConfig.current_prescript_target = "";
        sysConfig.save();
        if (out_code)
            *out_code = "CLEARED";
        SysCmdResult_Ok("CLEARED");
        Serial.println("[使用者] 已清空当前使用者。");
        return true;
    }

    if (!TargetExists(id))
    {
        if (out_code)
            *out_code = "NOT_FOUND";
        SysCmdResult_Error("NOT_FOUND", id);
        Serial.printf("[使用者] 选择失败，未找到使用者：%s\n", id.c_str());
        return false;
    }

    sysConfig.current_prescript_target = id;
    sysConfig.save();
    if (out_code)
        *out_code = "SELECTED";
    SysCmdResult_Ok("SELECTED", id);
    Serial.printf("[使用者] 已选中使用者：%s\n", id.c_str());
    return true;
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

void SysPrescriptTarget_SyncBLE()
{
    String current = EscapeJson(sysConfig.current_prescript_target);
    String head = "SYNC:TGT:{\"current\":\"" + current + "\",\"items\":[";

    for (int i = 0; i < sysConfig.prescript_target_count; i++)
    {
        if (i > 0)
            head += ",";
        head += "\"";
        head += EscapeJson(sysConfig.prescript_targets[i]);
        head += "\"";
    }
    head += "]}";

    SysBLE_Notify(head.c_str());
    delay(20);
}
