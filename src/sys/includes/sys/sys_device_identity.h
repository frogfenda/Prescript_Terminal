/*
【模块职责】设备身份凭据接口。机器码来自芯片 eFuse；服务器分配的公开 ID 与设备通行码保存在独立 identity NVS 分区。
【安全边界】公开查询绝不返回设备通行码；通行码只允许由未来的认证网络会话显式复制使用，调用方不得打印或写入普通配置。
*/
#pragma once

#include <Arduino.h>

enum class SysDeviceIdentityBindResult : uint8_t
{
    Ok = 0,
    StorageUnavailable,
    MachineCodeUnavailable,
    AlreadyBound,
    InvalidPublicId,
    InvalidDeviceKey,
    SaveFailed
};

struct SysDeviceIdentitySnapshot
{
    bool storage_ready = false;
    bool provisioned = false;
    uint16_t schema_version = 1;
    uint32_t credential_version = 0;
    String machine_code;
    String public_id;
};

// 【接口说明】初始化专用 NVS 分区并读取身份记录；失败不会阻止终端其他本地功能启动。
bool SysDeviceIdentity_Init();

// 【接口说明】返回不含秘密字段的身份快照，适合 UI、BLE 查询与诊断信息展示。
SysDeviceIdentitySnapshot SysDeviceIdentity_GetSnapshot();

// 【接口说明】生成 GET:IDENTITY 的安全响应文本；其中不含 device_key。
String SysDeviceIdentity_BuildPublicResponse();

// 【接口说明】首次写入服务器分配的公开 ID 与设备通行码；已绑定设备不会被该入口覆盖。
SysDeviceIdentityBindResult SysDeviceIdentity_Bind(const String &public_id, const String &device_key);

/*
 * 【接口说明】供未来 HTTPS 会话创建流程复制认证材料。
 * 该函数不会联网；machine_code/device_key 都属于认证材料，调用方使用后应尽快清空且严禁写日志。
 */
bool SysDeviceIdentity_CopyAuthCredentials(String &machine_code, String &device_key);

