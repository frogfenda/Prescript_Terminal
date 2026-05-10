/*
【模块职责】文本协议解析实现。识别 GET/TXT/ALM/SCH/PRE/WIFI/COIN/SPC 等命令，校验日期、时间、槽位和必填字段，并保留 | 与换行宏命令兼容。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_protocol.cpp
#include "sys_protocol.h"
#include "../lang/terminal_lang.h"
#include "sys_constants.h"
#include <time.h>

// 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
static SysParsedCommand MakeInvalid(const String &raw, const String &reason)
{
    SysParsedCommand out;
    out.type = SysCommandType::Invalid;
    out.raw = raw;
    out.error = reason;
    return out;
}

// 【函数说明】检查一组冒号位置是否都有效，ALM/SCH/POM/COIN 这类固定字段协议用它判定格式完整。
static bool HasAllSeparators(int count, const int *positions)
{
    for (int i = 0; i < count; ++i)
    {
        if (positions[i] <= 0)
            return false;
    }
    return true;
}


// 【函数说明】校验日期时间真实存在；先做范围检查，再用 mktime 归一化并反查年月日时分，避免 2 月 31 日这类伪日期。
static bool IsValidDateTime(int year, int month, int day, int hour, int minute)
{
    if (year < 2020 || year > 2099)
        return false;
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
        return false;

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    time_t normalized = mktime(&t);
    if (normalized == (time_t)-1)
        return false;

    return t.tm_year == year - 1900 &&
           t.tm_mon == month - 1 &&
           t.tm_mday == day &&
           t.tm_hour == hour &&
           t.tm_min == minute;
}

// 【函数说明】把 SCH 命令中的年月日时分转换成本地时区 timestamp，秒固定为 0。
static uint32_t MakeTimestamp(int year, int month, int day, int hour, int minute)
{
    time_t now;
    time(&now);
    struct tm t_info;
    localtime_r(&now, &t_info);
    t_info.tm_year = year - 1900;
    t_info.tm_mon = month - 1;
    t_info.tm_mday = day;
    t_info.tm_hour = hour;
    t_info.tm_min = minute;
    t_info.tm_sec = 0;
    return (uint32_t)mktime(&t_info);
}

// 【函数说明】解析 SCH/SCH_HID 命令：拆出年月日时分、标题和文本，校验时间并写入 hidden 标志。
static SysParsedCommand ParseSchedule(const String &msg, int prefix_len, bool hidden)
{
    int p1 = msg.indexOf(':', prefix_len);
    int p2 = msg.indexOf(':', p1 + 1);
    int p3 = msg.indexOf(':', p2 + 1);
    int p4 = msg.indexOf(':', p3 + 1);
    int p5 = msg.indexOf(':', p4 + 1);
    int p6 = msg.indexOf(':', p5 + 1);
    int positions[] = {p1, p2, p3, p4, p5, p6};
    if (!HasAllSeparators(6, positions))
        // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
        return MakeInvalid(msg, "SCH_FORMAT");

    SysParsedCommand out;
    out.type = SysCommandType::ScheduleAdd;
    out.raw = msg;
    out.hidden = hidden;
    int y = msg.substring(prefix_len, p1).toInt();
    int mo = msg.substring(p1 + 1, p2).toInt();
    int d = msg.substring(p2 + 1, p3).toInt();
    int h = msg.substring(p3 + 1, p4).toInt();
    int m = msg.substring(p4 + 1, p5).toInt();
    if (!IsValidDateTime(y, mo, d, h, m))
        // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
        return MakeInvalid(msg, "SCH_INVALID_TIME");

    out.timestamp = MakeTimestamp(y, mo, d, h, m);
    out.title = msg.substring(p5 + 1, p6);
    out.title.trim();
    out.text = msg.substring(p6 + 1);
    if (out.title.length() == 0)
        // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
        return MakeInvalid(msg, "SCH_EMPTY_TITLE");
    return out;
}

// 【函数说明】按命令前缀解析一条协议文本；每个分支只填 SysParsedCommand 字段，不发布事件也不保存配置。
SysParsedCommand SysProtocol_ParseSingle(const String &raw)
{
    String msg = raw;
    msg.trim();

    SysParsedCommand out;
    out.raw = msg;

    if (msg.length() == 0)
        // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
        return MakeInvalid(raw, "EMPTY");

    if (msg.startsWith("GET:LANG") || msg.startsWith("GET:INFO"))
    {
        out.type = SysCommandType::GetLanguage;
        return out;
    }

    if (msg.startsWith("GET:SYNC"))
    {
        out.type = SysCommandType::GetSync;
        int p = msg.indexOf(':', 9);
        if (p > 0)
            out.sync_lang = msg.substring(p + 1);
        return out;
    }

    if (msg.startsWith("GET:SPC_TXT:"))
    {
        out.type = SysCommandType::GetSpecialText;
        out.id = msg.substring(12);
        out.id.trim();
        if (out.id.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "SPC_TXT_EMPTY_ID");
        return out;
    }

    if (msg.startsWith("TXT:"))
    {
        out.type = SysCommandType::TextNotify;
        out.text = msg.substring(4);
        if (out.text.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "TXT_EMPTY");
        return out;
    }

    if (msg.startsWith("ALM:"))
    {
        int p1 = msg.indexOf(':', 4);
        int p2 = msg.indexOf(':', p1 + 1);
        int p3 = msg.indexOf(':', p2 + 1);
        int positions[] = {p1, p2, p3};
        if (!HasAllSeparators(3, positions))
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "ALM_FORMAT");
        out.type = SysCommandType::AlarmAdd;
        out.hour = msg.substring(4, p1).toInt();
        out.minute = msg.substring(p1 + 1, p2).toInt();
        out.name = msg.substring(p2 + 1, p3);
        out.name.trim();
        out.text = msg.substring(p3 + 1);
        if (out.hour < 0 || out.hour > 23 || out.minute < 0 || out.minute > 59)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "ALM_INVALID_TIME");
        if (out.name.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "ALM_EMPTY_NAME");
        return out;
    }

    if (msg.startsWith("ALM_DEL:"))
    {
        out.type = SysCommandType::AlarmDel;
        out.name = msg.substring(8);
        if (out.name.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "ALM_DEL_EMPTY_NAME");
        return out;
    }

    if (msg.startsWith("POM:"))
    {
        int p1 = msg.indexOf(':', 4);
        int p2 = msg.indexOf(':', p1 + 1);
        int p3 = msg.indexOf(':', p2 + 1);
        int positions[] = {p1, p2, p3};
        if (!HasAllSeparators(3, positions))
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "POM_FORMAT");
        out.type = SysCommandType::PomodoroUpdate;
        out.index = msg.substring(4, p1).toInt();
        out.name = msg.substring(p1 + 1, p2);
        out.name.trim();
        out.work_min = msg.substring(p2 + 1, p3).toInt();
        out.rest_min = msg.substring(p3 + 1).toInt();
        if (out.index < 0 || out.index >= PrescriptConst::MAX_POMODORO_PRESETS)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "POM_INVALID_SLOT");
        if (out.work_min <= 0 || out.rest_min <= 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "POM_INVALID_DURATION");
        return out;
    }

    if (msg.startsWith("SCH:"))
        // 【函数说明】解析 SCH/SCH_HID 命令：拆出年月日时分、标题和文本，校验时间并写入 hidden 标志。
        return ParseSchedule(msg, 4, false);

    if (msg.startsWith("SCH_HID:"))
        // 【函数说明】解析 SCH/SCH_HID 命令：拆出年月日时分、标题和文本，校验时间并写入 hidden 标志。
        return ParseSchedule(msg, 8, true);

    if (msg.startsWith("SCH_DEL:"))
    {
        out.type = SysCommandType::ScheduleDel;
        out.title = msg.substring(8);
        if (out.title.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "SCH_DEL_EMPTY_TITLE");
        return out;
    }

    if (msg.startsWith("PRE:ZH:"))
    {
        out.type = SysCommandType::PrescriptAdd;
        out.lang = LANG_ZH;
        out.text = msg.substring(7);
        if (out.text.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "PRE_EMPTY_TEXT");
        return out;
    }

    if (msg.startsWith("PRE:EN:"))
    {
        out.type = SysCommandType::PrescriptAdd;
        out.lang = LANG_EN;
        out.text = msg.substring(7);
        if (out.text.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "PRE_EMPTY_TEXT");
        return out;
    }

    if (msg.startsWith("PRE_DEL:ZH:"))
    {
        out.type = SysCommandType::PrescriptDel;
        out.lang = LANG_ZH;
        out.text = msg.substring(11);
        if (out.text.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "PRE_DEL_EMPTY_TEXT");
        return out;
    }

    if (msg.startsWith("PRE_DEL:EN:"))
    {
        out.type = SysCommandType::PrescriptDel;
        out.lang = LANG_EN;
        out.text = msg.substring(11);
        if (out.text.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "PRE_DEL_EMPTY_TEXT");
        return out;
    }

    if (msg.startsWith("WIFI:"))
    {
        out.type = SysCommandType::WifiSet;
        int p1 = msg.indexOf(':', 5);
        if (p1 > 0)
        {
            out.ssid = msg.substring(5, p1);
            out.pass = msg.substring(p1 + 1);
        }
        else
        {
            out.ssid = msg.substring(5);
            if (out.ssid.endsWith(":"))
                out.ssid = out.ssid.substring(0, out.ssid.length() - 1);
            out.pass = "";
        }
        if (out.ssid.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "WIFI_EMPTY_SSID");
        return out;
    }

    if (msg.startsWith("COIN:"))
    {
        int p1 = msg.indexOf(':', 5);
        int p2 = msg.indexOf(':', p1 + 1);
        int p3 = msg.indexOf(':', p2 + 1);
        int p4 = msg.indexOf(':', p3 + 1);
        int positions[] = {p1, p2, p3, p4};
        if (!HasAllSeparators(4, positions))
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "COIN_FORMAT");
        out.type = SysCommandType::CoinAdd;
        out.base_power = msg.substring(5, p1).toInt();
        out.coin_power = msg.substring(p1 + 1, p2).toInt();
        out.coin_count = msg.substring(p2 + 1, p3).toInt();
        out.colors = msg.substring(p3 + 1, p4);
        out.name = msg.substring(p4 + 1);
        out.name.trim();
        if (out.name.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "COIN_EMPTY_NAME");
        if (out.coin_count < 1 || out.coin_count > 9)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "COIN_INVALID_COUNT");
        return out;
    }

    if (msg.startsWith("COIN_DEL:"))
    {
        out.type = SysCommandType::CoinDel;
        out.name = msg.substring(9);
        if (out.name.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "COIN_DEL_EMPTY_NAME");
        return out;
    }

    if (msg.startsWith("SPC:"))
    {
        out.type = SysCommandType::SpecialForce;
        out.id = msg.substring(4);
        out.id.trim();
        if (out.id.length() == 0)
            // 【函数说明】构造 Invalid 解析结果，保留原始命令和错误代码，Router 会把 error 转成 ACK:ERR。
            return MakeInvalid(msg, "SPC_EMPTY_ID");
        return out;
    }

    out.type = SysCommandType::Unknown;
    return out;
}

// 【函数说明】从宏命令中按最近的换行或 | 切出一条子命令，跳过空白子串以兼容网页批量注入。
bool SysProtocol_NextMacroCommand(const String &raw, int &cursor, String &out_cmd)
{
    while (cursor < raw.length())
    {
        int pos1 = raw.indexOf('\n', cursor);
        int pos2 = raw.indexOf('|', cursor);
        int end = -1;

        if (pos1 == -1)
            end = pos2;
        else if (pos2 == -1)
            end = pos1;
        else
            end = (pos1 < pos2) ? pos1 : pos2;

        if (end == -1)
            end = raw.length();

        out_cmd = raw.substring(cursor, end);
        out_cmd.trim();
        cursor = end + 1;

        if (out_cmd.length() > 0)
            return true;
    }
    return false;
}

const char *SysProtocol_CommandName(SysCommandType type)
{
    switch (type)
    {
    case SysCommandType::GetSync:
        return "GET_SYNC";
    case SysCommandType::GetLanguage:
        return "GET_LANG";
    case SysCommandType::TextNotify:
        return "TXT";
    case SysCommandType::AlarmAdd:
        return "ALM";
    case SysCommandType::AlarmDel:
        return "ALM_DEL";
    case SysCommandType::PomodoroUpdate:
        return "POM";
    case SysCommandType::ScheduleAdd:
        return "SCH";
    case SysCommandType::ScheduleDel:
        return "SCH_DEL";
    case SysCommandType::PrescriptAdd:
        return "PRE";
    case SysCommandType::PrescriptDel:
        return "PRE_DEL";
    case SysCommandType::WifiSet:
        return "WIFI";
    case SysCommandType::CoinAdd:
        return "COIN";
    case SysCommandType::CoinDel:
        return "COIN_DEL";
    case SysCommandType::SpecialForce:
        return "SPC";
    case SysCommandType::GetSpecialText:
        return "GET_SPC_TXT";
    case SysCommandType::Invalid:
        return "INVALID";
    case SysCommandType::Unknown:
    default:
        return "UNKNOWN";
    }
}

// 【函数说明】标记会触发弹窗、写文件、改配置的命令，Router 只对这些命令捕获业务结果并回 ACK。
bool SysProtocol_IsMutating(SysCommandType type)
{
    switch (type)
    {
    case SysCommandType::TextNotify:
    case SysCommandType::AlarmAdd:
    case SysCommandType::AlarmDel:
    case SysCommandType::PomodoroUpdate:
    case SysCommandType::ScheduleAdd:
    case SysCommandType::ScheduleDel:
    case SysCommandType::PrescriptAdd:
    case SysCommandType::PrescriptDel:
    case SysCommandType::WifiSet:
    case SysCommandType::CoinAdd:
    case SysCommandType::CoinDel:
    case SysCommandType::SpecialForce:
        return true;
    default:
        return false;
    }
}
