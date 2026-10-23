/*
【模块职责】固件内部命令注册表。协议层只负责把文本解析成 SysParsedCommand，注册表负责
根据命令类型执行所属业务入口，并集中维护外部 ACK 名称和是否需要业务回执。
【重要约束】Handler 在 Arduino 主循环同步执行；BLE 回调仍只能入队，不能直接调用本模块。
*/
#pragma once

#include "sys/sys_protocol.h"

using SysCommandHandler = bool (*)(const SysParsedCommand &command);

struct SysCommandDescriptor
{
    SysCommandType type;
    const char *ack_name;
    bool mutating;
    SysCommandHandler handler;
};

const SysCommandDescriptor *SysCommandRegistry_Find(SysCommandType type);
bool SysCommandRegistry_Dispatch(const SysParsedCommand &command);
const char *SysCommandRegistry_CommandName(SysCommandType type);
bool SysCommandRegistry_IsMutating(SysCommandType type);
