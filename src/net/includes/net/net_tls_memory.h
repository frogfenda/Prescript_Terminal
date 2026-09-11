/*
【模块职责】为 mbedTLS 提供受预算约束的 PSRAM 动态分配器，并记录单次 HTTPS 请求的内存峰值。
【调用关系】NetService 初始化时安装一次全局分配器；NetHttpTransport 在每次请求边界重置并读取诊断。
【重要约束】mbedTLS 的分配器是进程级全局状态，安装后不得在业务请求之间反复切换。
*/
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class NetTlsMemoryFailure : uint8_t
{
    None,
    InvalidSize,
    BudgetExceeded,
    PsramExhausted,
};

struct NetTlsMemorySnapshot
{
    bool installed = false;
    size_t budget_bytes = 0;
    size_t current_bytes = 0;
    size_t request_peak_bytes = 0;
    size_t lifetime_peak_bytes = 0;
    size_t last_failed_request_bytes = 0;
    uint32_t allocation_failures = 0;
    NetTlsMemoryFailure last_failure = NetTlsMemoryFailure::None;
};

/**
 * 【接口说明】把 mbedTLS 的 calloc/free 安装为 PSRAM 分配器，并设置固定的逻辑预算。
 * 【返回语义】只有 PSRAM 可用且官方 mbedTLS 运行时接口安装成功时返回 true。
 * 【调用约束】必须在第一次 TLS 对象创建前调用；重复调用安全且不会重置统计。
 */
bool NetTlsMemory_InstallAllocator();

/** 【接口说明】开始记录一轮 HTTPS 请求的峰值与失败原因；不清除生命周期峰值。 */
void NetTlsMemory_BeginRequest();

/** 【接口说明】取得原子快照，供低频串口诊断使用；不会输出密钥或请求正文。 */
NetTlsMemorySnapshot NetTlsMemory_GetSnapshot();

/** 【接口说明】将稳定失败枚举转换为中文短语，只用于诊断输出。 */
const char *NetTlsMemory_DescribeFailure(NetTlsMemoryFailure failure);
