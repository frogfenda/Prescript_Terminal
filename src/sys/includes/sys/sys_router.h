/*
【模块职责】系统命令入口接口。外部入口把原始 BLE 字符串或 NFC 文本交给统一解析和命令注册表，
注册表直接调用所属业务 Handler；只有同步广播和 NFC 输入继续使用事件总线。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_router.h
#pragma once
#include <Arduino.h>

// 【接口说明】处理 WebBLE 和 NFC 复用的文本命令入口；内部会拆宏命令、解析协议、直接执行 Handler 并回传 ACK。
void SysRouter_ProcessBLE(const String& msg);

// 【扩展约束】新增命令先在 sys_protocol 中完成解析，再在 sys_command_registry.cpp 注册 Handler、ACK 名称和回执属性。

// 初始化路由器内部事件订阅，例如 NFC 物理卡片入口。
// 【接口说明】注册 NFC 扫描事件回调，使实体 NFC 卡片内容进入与 BLE 相同的命令路由。
void SysRouter_Init();
