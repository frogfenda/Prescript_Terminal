/*
【模块职责】生成设备身份绑定协议 v1 的 HMAC 挑战应答。
【安全边界】本模块不联网、不存永久通行码，也绝不输出产品绑定主密钥或中间派生密钥。
*/
#pragma once

#include <Arduino.h>

/** 当前固件是否注入了可用的产品绑定主密钥。 */
bool SysDeviceBinding_IsProofAvailable();

/** 注册 BIND 命令组（只允许蓝牙来源）；由 main.cpp 在启动时调用一次。 */
void SysDeviceBinding_RegisterCommands();

/**
 * 进入一次性的 App 账号认领模式；只保留在 RAM，默认十分钟后失效。
 * 进入和退出（包括超时）都会向已连接的客户端推送 EVT:BIND.MODE。
 */
void SysDeviceBinding_EnterMode();
void SysDeviceBinding_ExitMode();
void SysDeviceBinding_Update();
bool SysDeviceBinding_IsModeActive();

/** 在绑定模式中为当前账号挑战生成一次性证明；证明本身只通过 BLE 返回。 */
bool SysDeviceBinding_BuildModeProof(
    const String &challenge_id,
    const String &challenge,
    String &verification_code);

/** 只有刚刚生成过的证明才能提交 V2 永久凭据，成功后立即消费。 */
bool SysDeviceBinding_IsCredentialCommitAllowed();
void SysDeviceBinding_ConsumeCredentialCommit();

/**
 * 按协议 v1 生成 URL-safe、无填充的校验码。
 * machine_code、challenge_id 和 challenge 均来自本轮绑定上下文，调用方不得自行改写规范格式。
 */
bool SysDeviceBinding_BuildVerificationCode(
    const String &machine_code,
    const String &challenge_id,
    const String &challenge,
    String &verification_code);
