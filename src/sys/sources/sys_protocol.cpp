/*
【模块职责】协议 v2 行解析实现。只做字符级处理，所有输出写进调用方提供的缓冲区。
*/
#include "sys/sys_protocol.h"
#include <stdio.h>
#include <string.h>

namespace
{
    bool IsCodeChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    bool IsIdChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }

    // 读取 [begin, end) 中满足 pred 的连续字符到 out；长度必须在 1..out_size-1 之间。
    template <typename Pred>
    bool ReadToken(const char *&cursor, const char *end, char *out, size_t out_size, Pred pred)
    {
        size_t n = 0;
        while (cursor < end && pred(*cursor))
        {
            if (n + 1 >= out_size)
                return false;
            out[n++] = *cursor++;
        }
        out[n] = '\0';
        return n > 0;
    }
}

bool SysProtocol_ParseHead(const char *line, size_t length, SysCmdHead &out)
{
    out.group[0] = out.verb[0] = out.id[0] = '\0';
    out.args = nullptr;
    out.args_length = 0;
    if (line == nullptr)
        return false;

    const char *cursor = line;
    const char *end = line + length;
    if (!ReadToken(cursor, end, out.group, sizeof(out.group), IsCodeChar))
        return false;
    if (cursor >= end || *cursor != '.')
        return false;
    ++cursor;
    if (!ReadToken(cursor, end, out.verb, sizeof(out.verb), IsCodeChar))
        return false;
    if (cursor < end && *cursor == '#')
    {
        ++cursor;
        if (!ReadToken(cursor, end, out.id, sizeof(out.id), IsIdChar))
            return false;
    }
    if (cursor == end)
        return true;
    if (*cursor != ':')
        return false;
    out.args = cursor + 1;
    out.args_length = static_cast<size_t>(end - out.args);
    return true;
}

void SysProtocol_ExtractReplyTarget(const char *text, char *name, size_t name_size, char *id, size_t id_size)
{
    if (name_size > 0)
        strncpy(name, "?", name_size);
    if (id_size > 0)
        id[0] = '\0';
    if (text == nullptr || name_size < 2)
        return;

    // 只看到第一个冒号为止的请求头；截断的行在这里也能取出组.动词和编号。
    const char *colon = strchr(text, ':');
    const size_t head_length = colon ? static_cast<size_t>(colon - text) : strlen(text);
    SysCmdHead head;
    if (!SysProtocol_ParseHead(text, head_length, head))
        return;
    snprintf(name, name_size, "%s.%s", head.group, head.verb);
    if (id_size > 0)
        snprintf(id, id_size, "%s", head.id);
}

int SysProtocol_SplitArgs(const char *args, size_t length, uint8_t max_fields, bool rest_last,
                          char *out_buffer, size_t buffer_size, const char **fields, uint16_t *lengths)
{
    if (args == nullptr)
        return 0;
    if (max_fields == 0)
        return SYS_PROTOCOL_ARGS_TOO_MANY;

    size_t out = 0;
    int count = 0;
    size_t field_start = 0;
    bool field_open = false;

    auto open_field = [&]() -> bool {
        if (count >= max_fields)
            return false;
        field_start = out;
        fields[count] = out_buffer + out;
        field_open = true;
        return true;
    };
    auto close_field = [&]() -> bool {
        if (out >= buffer_size)
            return false;
        lengths[count] = static_cast<uint16_t>(out - field_start);
        out_buffer[out++] = '\0';
        ++count;
        field_open = false;
        return true;
    };

    if (!open_field())
        return SYS_PROTOCOL_ARGS_TOO_MANY;

    for (size_t i = 0; i < length; ++i)
    {
        char c = args[i];
        const bool in_rest = rest_last && count == max_fields - 1;
        if (c == '\\' && i + 1 < length)
        {
            const char next = args[i + 1];
            if (next == ':' || next == '\\' || next == 'n')
            {
                c = (next == 'n') ? '\n' : next;
                ++i;
            }
        }
        else if (c == ':' && !in_rest)
        {
            if (!close_field())
                return SYS_PROTOCOL_ARGS_NO_SPACE;
            if (!open_field())
                return SYS_PROTOCOL_ARGS_TOO_MANY;
            continue;
        }
        if (out + 1 >= buffer_size)
            return SYS_PROTOCOL_ARGS_NO_SPACE;
        out_buffer[out++] = c;
    }
    if (field_open && !close_field())
        return SYS_PROTOCOL_ARGS_NO_SPACE;
    return count;
}
