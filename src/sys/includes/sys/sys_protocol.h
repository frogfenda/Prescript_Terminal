/*
【模块职责】协议 v2 行解析。把一行请求拆成“组.动词”、可选编号和参数字段，并处理转义。
【重要约束】纯函数：不依赖 Arduino String、不分配内存、不执行命令，可在主机端单独测试。
行格式见 docs/ble-protocol-v2.md 第 3 节：<组>.<动词>[#<编号>][:<参数1>[:<参数2>...]]
*/
#pragma once
#include <stddef.h>
#include <stdint.h>

// 请求头的长度上限：组码 1～11 字符，动词 1～15 字符，编号 1～8 字符（各自留 1 字节给 \0）。
struct SysCmdHead
{
    char group[12];
    char verb[16];
    char id[9];
    const char *args;  // 指向第一个冒号之后；没有参数区时为 nullptr
    size_t args_length;
};

/**
 * 【接口说明】解析请求头。line 不要求以 \0 结尾。
 * 成功返回 true，并让 out.args 指向参数区（只借用 line，不复制）；
 * 组码、动词或编号的字符或长度不合法时返回 false，调用方应回 BAD_REQUEST。
 */
bool SysProtocol_ParseHead(const char *line, size_t length, SysCmdHead &out);

/**
 * 【接口说明】把外部 #RRGGBB（大小写均可）转换成屏幕 RGB565。
 * BLE 自定义推送和网络邮件共用；纯函数、可跨核调用。
 * 严格拒绝缺少 #、长度不符及非十六进制字符；失败时不修改 out，不在这里决定默认颜色。
 */
bool SysProtocol_ParseColor(const char *value, uint16_t &out);

/**
 * 【接口说明】从任意一段请求开头（可能已被截断或不合法）尽量取出“组.动词”和编号，用于拒收时的错误回包。
 * 取不到时 name 写成 "?"、id 写成空串。
 */
void SysProtocol_ExtractReplyTarget(const char *text, char *name, size_t name_size, char *id, size_t id_size);

// SplitArgs 的特殊返回值。
constexpr int SYS_PROTOCOL_ARGS_TOO_MANY = -1;   // 字段数超过 max_fields
constexpr int SYS_PROTOCOL_ARGS_NO_SPACE = -2;   // 输出缓冲不够

/**
 * 【接口说明】把参数区按冒号拆成字段并反转义（\: → 冒号，\\ → 反斜杠，\n → 换行；其他反斜杠原样保留）。
 * - rest_last 为 true 时，第 max_fields 个字段收下剩余全部内容，其中未转义的冒号原样保留；
 * - 字段依次写入 out_buffer，各以 \0 结尾；fields[i] 指向第 i 个字段，lengths[i] 为其字节数；
 * - args 为 nullptr 表示没有参数区，返回 0；"A:" 这样的结尾冒号会产生一个空字段。
 * 返回字段数，或上面两个负值之一。
 */
int SysProtocol_SplitArgs(const char *args, size_t length, uint8_t max_fields, bool rest_last,
                          char *out_buffer, size_t buffer_size, const char **fields, uint16_t *lengths);
