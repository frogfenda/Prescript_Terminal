/*
【模块职责】文本协议解析接口。把 BLE、NFC、API 传入的字符串命令转换为 SysParsedCommand，不发布事件、不写文件。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_protocol.h
#pragma once
#include <Arduino.h>

// Stage 5: 纯协议解析层。
// 这里只把 BLE/NFC/API 的文本命令解析成结构化命令，不发布事件、不触发 UI。

enum class SysCommandType : uint8_t
{
    Unknown = 0,
    Invalid,
    GetSync,
    GetLanguage,
    TextNotify,
    AlarmAdd,
    AlarmDel,
    PomodoroUpdate,
    ScheduleAdd,
    ScheduleDel,
    PrescriptAdd,
    PrescriptDel,
    WifiSet,
    CoinAdd,
    CoinDel,
    SpecialForce,
    GetSpecialText
};

struct SysParsedCommand
{
    SysCommandType type = SysCommandType::Unknown;
    String raw;
    String error;

    // 通用字符串参数
    String name;
    String title;
    String text;
    String id;
    String ssid;
    String pass;
    String colors;
    String sync_lang;

    // 通用数值参数
    int lang = 0;
    int hour = 0;
    int minute = 0;
    int index = 0;
    int work_min = 0;
    int rest_min = 0;
    int base_power = 0;
    int coin_power = 0;
    int coin_count = 0;
    uint32_t timestamp = 0;
    bool hidden = false;
};

// 从一条完整命令解析出结构体。
// 【接口说明】解析一条已经分割好的命令，返回类型、文本、时间、语言、硬币参数等结构化字段；错误时 type=Invalid 并给出 error。
SysParsedCommand SysProtocol_ParseSingle(const String &raw);

// 宏命令分割器：支持 \n 和 |，保持旧协议兼容。
// cursor 初始传 0；返回 true 代表拿到了一条非空子命令。
// 【接口说明】从包含 | 和换行的宏命令字符串中取出下一条非空子命令，cursor 记录扫描位置。
bool SysProtocol_NextMacroCommand(const String &raw, int &cursor, String &out_cmd);

const char *SysProtocol_CommandName(SysCommandType type);
// 【接口说明】判断命令是否会改变设备状态；Router 只对这类命令开启业务回执捕获并发送 ACK。
bool SysProtocol_IsMutating(SysCommandType type);
