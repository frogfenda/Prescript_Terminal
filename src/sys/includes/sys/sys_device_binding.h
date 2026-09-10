/*
【模块职责】生成设备自动绑定协议 v1 的 HMAC 挑战应答。
【安全边界】本模块不联网、不存永久通行码，也绝不输出产品绑定主密钥或中间派生密钥。
*/
#pragma once

#include <Arduino.h>

/** 当前固件是否注入了可用的产品绑定主密钥。 */
bool SysDeviceBinding_IsProofAvailable();

/**
 * 按协议 v1 生成 URL-safe、无填充的校验码。
 * machine_code、challenge_id 和 challenge 均来自本轮绑定上下文，调用方不得自行改写规范格式。
 */
bool SysDeviceBinding_BuildVerificationCode(
    const String &machine_code,
    const String &challenge_id,
    const String &challenge,
    String &verification_code);
