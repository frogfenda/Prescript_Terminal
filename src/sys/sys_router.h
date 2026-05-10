/*
【模块职责】系统命令路由接口。外部入口把原始 BLE 字符串、NFC 文本、网络 API 日程交给这里，路由层再发布 SysEvent。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_router.h
#pragma once
#include <Arduino.h>

// 处理蓝牙发来的杂乱字符串
// 【接口说明】处理 WebBLE 和 NFC 复用的文本命令入口；内部会拆宏命令、解析协议、发布事件和回传 ACK。
void SysRouter_ProcessBLE(const String& msg);

// 处理网络 API 截获的隐秘指令
// 【接口说明】处理网络 API 拉取到的隐藏日程，直接转换为 EVT_SCHEDULE_ADD 并标记 hidden。
void SysRouter_ProcessAPI(uint32_t tt, const String& title, const String& text);
// 初始化路由器内部事件订阅，例如 NFC 物理卡片入口。
// 【接口说明】注册 NFC 扫描事件回调，使实体 NFC 卡片内容进入与 BLE 相同的命令路由。
void SysRouter_Init();
