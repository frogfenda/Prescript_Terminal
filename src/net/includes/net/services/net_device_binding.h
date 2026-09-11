/* 【模块职责】注册只能由用户从设置页显式启动的设备首次绑定任务。 */
#pragma once

#include <Arduino.h>

constexpr uint16_t NET_TASK_DEVICE_BINDING = 2;

/**
 * 首次身份绑定的稳定失败分类。网络任务只发布类型，不把服务端响应正文或秘密材料暴露给 UI。
 * 调用方应在对应在线任务完成后读取；None 表示成功或尚无失败记录。
 */
enum class NetDeviceBindingFailure : uint8_t
{
    None,
    Unknown,
    IdentityUnavailable,
    BindingSecretUnavailable,
    RequestTimeout,
    SecureConnectionFailed,
    ServerConnectionFailed,
    RequestSendFailed,
    ConnectionLost,
    InsufficientMemory,
    NetworkTransportFailed,
    ServerAlreadyBound,
    ServerRejected,
    ServerUnavailable,
    InvalidServerResponse,
    ProofGenerationFailed,
    CredentialSaveFailed,
    SessionAuthenticationFailed,
};

bool NetDeviceBinding_Register();

/** 跨核心安全地读取最近一次身份绑定任务的失败分类。 */
NetDeviceBindingFailure NetDeviceBinding_GetLastFailure();
