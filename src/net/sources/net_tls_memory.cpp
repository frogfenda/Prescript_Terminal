/*
【模块职责】实现 mbedTLS 的 PSRAM calloc/free 适配和 96 KiB 逻辑预算。
【内存模型】每个返回块前保存固定头部，用于 free 时精确回收统计；实际存储仍由 ESP-IDF
能力型堆管理，不自行实现容易碎片化的物理内存池。
【并发约束】分配器是全局入口，统计由临界区保护；真正的堆申请不在临界区内执行。
*/
#include "net/net_tls_memory.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <mbedtls/platform.h>
#include <soc/soc_memory_types.h>

#include <limits.h>

namespace
{
    constexpr size_t kTlsPsramBudgetBytes = 96U * 1024U;
    constexpr uint32_t kAllocationMagic = 0x544C534DU; // "TLSM"

    struct alignas(16) AllocationHeader
    {
        uint32_t magic;
        uint32_t reserved;
        size_t allocation_bytes;
        size_t requested_bytes;
    };

    static_assert((sizeof(AllocationHeader) % 16U) == 0U,
                  "TLS allocation header must preserve allocator alignment");

    portMUX_TYPE s_statsMux = portMUX_INITIALIZER_UNLOCKED;
    bool s_installed = false;
    size_t s_currentBytes = 0;
    size_t s_requestPeakBytes = 0;
    size_t s_lifetimePeakBytes = 0;
    size_t s_lastFailedRequestBytes = 0;
    uint32_t s_allocationFailures = 0;
    NetTlsMemoryFailure s_lastFailure = NetTlsMemoryFailure::None;

    void RecordFailure(NetTlsMemoryFailure failure, size_t requestedBytes)
    {
        portENTER_CRITICAL(&s_statsMux);
        s_lastFailure = failure;
        s_lastFailedRequestBytes = requestedBytes;
        ++s_allocationFailures;
        portEXIT_CRITICAL(&s_statsMux);
    }

    bool ReserveBudget(size_t allocationBytes, size_t requestedBytes)
    {
        bool reserved = false;
        portENTER_CRITICAL(&s_statsMux);
        if (allocationBytes <= kTlsPsramBudgetBytes - s_currentBytes)
        {
            s_currentBytes += allocationBytes;
            if (s_currentBytes > s_requestPeakBytes)
                s_requestPeakBytes = s_currentBytes;
            if (s_currentBytes > s_lifetimePeakBytes)
                s_lifetimePeakBytes = s_currentBytes;
            reserved = true;
        }
        else
        {
            s_lastFailure = NetTlsMemoryFailure::BudgetExceeded;
            s_lastFailedRequestBytes = requestedBytes;
            ++s_allocationFailures;
        }
        portEXIT_CRITICAL(&s_statsMux);
        return reserved;
    }

    void ReleaseBudget(size_t allocationBytes)
    {
        portENTER_CRITICAL(&s_statsMux);
        s_currentBytes = allocationBytes <= s_currentBytes
                             ? s_currentBytes - allocationBytes
                             : 0;
        portEXIT_CRITICAL(&s_statsMux);
    }

    /**
     * mbedTLS 要求 calloc 语义，因此这里同时完成乘法溢出检查和清零。
     * 预算包含私有头部，使统计值能够代表 TLS 对 PSRAM 的真实占用上界。
     */
    void *TlsPsramCalloc(size_t count, size_t elementSize)
    {
        if (elementSize != 0U && count > SIZE_MAX / elementSize)
        {
            RecordFailure(NetTlsMemoryFailure::InvalidSize, SIZE_MAX);
            return nullptr;
        }

        const size_t requestedBytes = count * elementSize;
        if (requestedBytes > SIZE_MAX - sizeof(AllocationHeader))
        {
            RecordFailure(NetTlsMemoryFailure::InvalidSize, requestedBytes);
            return nullptr;
        }

        const size_t allocationBytes = sizeof(AllocationHeader) + requestedBytes;
        if (!ReserveBudget(allocationBytes, requestedBytes))
            return nullptr;

        AllocationHeader *header = static_cast<AllocationHeader *>(
            heap_caps_calloc(1U,
                             allocationBytes,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!header)
        {
            ReleaseBudget(allocationBytes);
            RecordFailure(NetTlsMemoryFailure::PsramExhausted, requestedBytes);
            return nullptr;
        }

        header->magic = kAllocationMagic;
        header->allocation_bytes = allocationBytes;
        header->requested_bytes = requestedBytes;
        return header + 1;
    }

    void TlsPsramFree(void *pointer)
    {
        if (!pointer)
            return;

        /*
         * 安装发生在首次 HTTPS 之前，正常情况下所有指针都有私有头部。
         * 仍保留内部 RAM 兼容分支，避免某个框架级 TLS 对象早于网络服务创建时误读头部。
         */
        if (!esp_ptr_external_ram(pointer))
        {
            heap_caps_free(pointer);
            return;
        }

        AllocationHeader *header = static_cast<AllocationHeader *>(pointer) - 1;
        if (header->magic != kAllocationMagic)
        {
            heap_caps_free(pointer);
            return;
        }

        const size_t allocationBytes = header->allocation_bytes;
        header->magic = 0;
        ReleaseBudget(allocationBytes);
        heap_caps_free(header);
    }
}

bool NetTlsMemory_InstallAllocator()
{
    portENTER_CRITICAL(&s_statsMux);
    const bool alreadyInstalled = s_installed;
    portEXIT_CRITICAL(&s_statsMux);
    if (alreadyInstalled)
        return true;

    const size_t totalPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (totalPsram < kTlsPsramBudgetBytes)
    {
        Serial.printf("[网络/TLS内存] 安装失败：PSRAM总量=%u，最低需要=%u。\n",
                      static_cast<unsigned>(totalPsram),
                      static_cast<unsigned>(kTlsPsramBudgetBytes));
        return false;
    }

    const int result = mbedtls_platform_set_calloc_free(TlsPsramCalloc, TlsPsramFree);
    if (result != 0)
    {
        Serial.printf("[网络/TLS内存] 安装官方分配器接口失败：错误=%d。\n", result);
        return false;
    }

    portENTER_CRITICAL(&s_statsMux);
    s_installed = true;
    portEXIT_CRITICAL(&s_statsMux);
    Serial.printf("[网络/TLS内存] 已启用PSRAM分配器，逻辑预算=%u字节。\n",
                  static_cast<unsigned>(kTlsPsramBudgetBytes));
    return true;
}

void NetTlsMemory_BeginRequest()
{
    portENTER_CRITICAL(&s_statsMux);
    s_requestPeakBytes = s_currentBytes;
    s_lastFailedRequestBytes = 0;
    s_lastFailure = NetTlsMemoryFailure::None;
    portEXIT_CRITICAL(&s_statsMux);
}

NetTlsMemorySnapshot NetTlsMemory_GetSnapshot()
{
    NetTlsMemorySnapshot snapshot;
    portENTER_CRITICAL(&s_statsMux);
    snapshot.installed = s_installed;
    snapshot.budget_bytes = kTlsPsramBudgetBytes;
    snapshot.current_bytes = s_currentBytes;
    snapshot.request_peak_bytes = s_requestPeakBytes;
    snapshot.lifetime_peak_bytes = s_lifetimePeakBytes;
    snapshot.last_failed_request_bytes = s_lastFailedRequestBytes;
    snapshot.allocation_failures = s_allocationFailures;
    snapshot.last_failure = s_lastFailure;
    portEXIT_CRITICAL(&s_statsMux);
    return snapshot;
}

const char *NetTlsMemory_DescribeFailure(NetTlsMemoryFailure failure)
{
    switch (failure)
    {
    case NetTlsMemoryFailure::None:
        return "无";
    case NetTlsMemoryFailure::InvalidSize:
        return "申请大小无效";
    case NetTlsMemoryFailure::BudgetExceeded:
        return "超过TLS专用预算";
    case NetTlsMemoryFailure::PsramExhausted:
        return "PSRAM空间不足";
    default:
        return "未知";
    }
}
