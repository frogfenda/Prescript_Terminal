/*
【模块职责】蓝牙传输层（协议 v2）。提供一个自定义服务：RX 特征接收请求字节流，TX 特征 Notify 回包字节流。
RX 字节由行组装器拼成完整行放入命令行队列；TX 字节先进 PSRAM 发送缓冲，再由蓝牙任务按当前 MTU 切块发送。
【重要约束】本层只搬运字节，不理解命令内容；行格式和回包格式见 docs/ble-protocol-v2.md。
*/
#pragma once
#include <Arduino.h>

// 【接口说明】启动蓝牙任务（Core 0）：创建公开信息/配对特征和加密业务特征、开始广播，并负责发送缓冲的切块发送。
void SysBLE_Init();

/** 启动 BLE 命令组注册；应在 SysCommand_Init() 之后、SysBLE_Init() 之前调用。 */
void SysBLE_RegisterCommands();

/** 公开配对状态；只由蓝牙主机任务/主循环读取，不返回密钥或认证材料。 */
bool SysBLE_IsPairingMode();
bool SysBLE_IsSecure();
uint32_t SysBLE_PairingRemainingMs();
/** 仅由设备设置页调用；replace_existing=true 时先断开旧手机并清空旧 bonding。 */
void SysBLE_EnterPairingMode(bool replace_existing = false);
void SysBLE_ExitPairingMode();
/** 仅允许已加密业务流程调用；清除 NimBLE 持久化 bonding 密钥。 */
bool SysBLE_ClearBonds();
/** 更新 GAP 广播名称并持久化后的运行时刷新。调用者必须在主循环调用。 */
bool SysBLE_SetDeviceName(const String &name);

/**
 * 【接口说明】把一段已经编码好的回包字节追加到发送缓冲；只能在主循环调用。
 * session 必须是生成本回包时取得的连接代次；断开、重新连接或取消订阅都会使旧代次失效。
 * 未就绪、代次不匹配或空间不足时整段不写入并返回 false；调用方只在同一代次内重试。
 */
bool SysBLE_Write(const char *data, size_t length, uint32_t session);

/** 【接口说明】读取连接/订阅代次；与请求和待发回包一起保存，避免快速重连串入旧数据。 */
uint32_t SysBLE_GetSession();

/** 【接口说明】发送缓冲总容量（分配失败为 0）；区分暂时拥塞和永远放不下的单行。 */
size_t SysBLE_TxCapacity();

/** 【接口说明】发送缓冲当前剩余字节数；仅反映传输环，不包含框架保留的待发快照。 */
size_t SysBLE_TxFree();

/** 【接口说明】是否有已连接、已完成 bonding 加密且订阅 TX 的客户端；没有时列表流可以直接结束。 */
bool SysBLE_IsLinkReady();
