// 文件：src/net/sources/net_outbox.cpp
/*
【模块职责】用独立 NVS 分区实现固定容量联网 Outbox。
【并发边界】主循环可入队，Core 0 网络任务可领取/确认；所有 NVS 事务由同一互斥锁串行化。
【掉电边界】领取只增加尝试次数，不写“执行中”状态；因此任何阶段掉电都不会把任务永久卡死。
*/
#include "net/net_outbox.h"

#include <cstddef>
#include <cstring>
#include <esp_err.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace
{
    constexpr char kPartitionLabel[] = "outbox";
    constexpr char kNamespace[] = "jobs";
    constexpr char kSequenceKey[] = "sequence";
    constexpr char kMigrationNamespace[] = "sys_migrate";
    constexpr char kMigrationKey[] = "outbox_v1";
    constexpr uint32_t kRecordMagic = 0x50544A42UL; // "PTJB"
    constexpr uint16_t kSchemaVersion = 1;
    constexpr int64_t kMinimumTrustedEpoch = 1000000000LL;

    struct __attribute__((packed)) StoredNetworkJob
    {
        uint32_t magic;
        uint16_t schema_version;
        uint16_t record_size;
        uint64_t job_id;
        uint32_t created_sequence;
        uint16_t task_id;
        uint8_t priority;
        uint8_t attempt_count;
        uint8_t state;
        uint8_t reserved;
        uint16_t payload_length;
        uint64_t dedup_key;
        int64_t not_before_epoch;
        uint8_t payload[NET_OUTBOX_PAYLOAD_MAX];
        uint32_t checksum;
    };

    static_assert(sizeof(StoredNetworkJob) == 432, "Outbox 记录布局变化时必须提升 schema_version");

    SemaphoreHandle_t s_mutex = nullptr;
    bool s_storageReady = false;

    class ScopedLock
    {
    public:
        ScopedLock() : locked_(s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {}
        ~ScopedLock()
        {
            if (locked_)
                xSemaphoreGive(s_mutex);
        }
        bool locked() const { return locked_; }

    private:
        bool locked_;
    };

    uint32_t CalculateChecksum(const StoredNetworkJob &record)
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
        const size_t length = offsetof(StoredNetworkJob, checksum);
        uint32_t hash = 2166136261UL;
        for (size_t i = 0; i < length; ++i)
        {
            hash ^= bytes[i];
            hash *= 16777619UL;
        }
        return hash;
    }

    void BuildSlotKey(uint8_t slot, char *output, size_t output_size)
    {
        snprintf(output, output_size, "job%02u", static_cast<unsigned>(slot));
    }

    bool IsRecordValid(const StoredNetworkJob &record)
    {
        return record.magic == kRecordMagic &&
               record.schema_version == kSchemaVersion &&
               record.record_size == sizeof(StoredNetworkJob) &&
               record.job_id != 0 &&
               record.task_id != 0 &&
               record.payload_length <= NET_OUTBOX_PAYLOAD_MAX &&
               record.state <= static_cast<uint8_t>(NetOutboxJobState::DeadLetter) &&
               record.not_before_epoch >= 0 &&
               record.checksum == CalculateChecksum(record);
    }

    void CopyPublicJob(const StoredNetworkJob &source, NetOutboxJob *target)
    {
        memset(target, 0, sizeof(*target));
        target->job_id = source.job_id;
        target->dedup_key = source.dedup_key;
        target->created_sequence = source.created_sequence;
        target->not_before_epoch = source.not_before_epoch;
        target->task_id = source.task_id;
        target->payload_length = source.payload_length;
        target->priority = source.priority;
        target->attempt_count = source.attempt_count;
        target->state = static_cast<NetOutboxJobState>(source.state);
        if (source.payload_length > 0)
            memcpy(target->payload, source.payload, source.payload_length);
    }

    esp_err_t ReadSlot(nvs_handle_t handle, uint8_t slot, StoredNetworkJob *record)
    {
        char key[8] = {};
        BuildSlotKey(slot, key, sizeof(key));
        size_t length = sizeof(*record);
        const esp_err_t error = nvs_get_blob(handle, key, record, &length);
        if (error == ESP_OK && (length != sizeof(*record) || !IsRecordValid(*record)))
            return ESP_ERR_INVALID_STATE;
        return error;
    }

    esp_err_t WriteSlot(nvs_handle_t handle, uint8_t slot, StoredNetworkJob *record)
    {
        record->checksum = CalculateChecksum(*record);
        char key[8] = {};
        BuildSlotKey(slot, key, sizeof(key));
        return nvs_set_blob(handle, key, record, sizeof(*record));
    }

    bool WasPartitionInitializedBefore()
    {
        nvs_handle_t handle = 0;
        if (nvs_open(kMigrationNamespace, NVS_READONLY, &handle) != ESP_OK)
            return false;
        uint8_t marker = 0;
        const esp_err_t error = nvs_get_u8(handle, kMigrationKey, &marker);
        nvs_close(handle);
        return error == ESP_OK && marker == 1;
    }

    bool SavePartitionInitializedMarker()
    {
        nvs_handle_t handle = 0;
        esp_err_t error = nvs_open(kMigrationNamespace, NVS_READWRITE, &handle);
        if (error == ESP_OK)
        {
            error = nvs_set_u8(handle, kMigrationKey, 1);
            if (error == ESP_OK)
                error = nvs_commit(handle);
            nvs_close(handle);
        }
        return error == ESP_OK;
    }

    /*
     * 新分区来自旧 LittleFS 尾部，首次升级时可能不是空白。默认 NVS 中的迁移标记只允许
     * 第一次自动格式化；正式投入使用后若 NVS 报错则保留现场，绝不把待发任务静默清空。
     */
    bool PrepareOutboxPartition()
    {
        const bool initialized_before = WasPartitionInitializedBefore();
        esp_err_t error = nvs_flash_init_partition(kPartitionLabel);
        if ((error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) &&
            !initialized_before)
        {
            Serial.println("[联网待办] 首次启用专用分区，只格式化 outbox 区域。");
            error = nvs_flash_erase_partition(kPartitionLabel);
            if (error == ESP_OK)
                error = nvs_flash_init_partition(kPartitionLabel);
        }

        if (error != ESP_OK)
        {
            Serial.printf("[联网待办] outbox 分区初始化失败：%s；为保留待办，未自动擦除。\n",
                          esp_err_to_name(error));
            return false;
        }

        if (!initialized_before && !SavePartitionInitializedMarker())
        {
            Serial.println("[联网待办] 无法保存首次初始化标记，拒绝启用以避免未来误格式化。");
            return false;
        }
        return true;
    }

    bool FindJobSlot(nvs_handle_t handle, uint64_t job_id, uint8_t *out_slot, StoredNetworkJob *out_record)
    {
        for (uint8_t slot = 0; slot < NET_OUTBOX_CAPACITY; ++slot)
        {
            StoredNetworkJob record = {};
            const esp_err_t error = ReadSlot(handle, slot, &record);
            if (error == ESP_ERR_NVS_NOT_FOUND)
                continue;
            if (error != ESP_OK)
                return false;
            if (record.job_id == job_id)
            {
                *out_slot = slot;
                *out_record = record;
                return true;
            }
        }
        return false;
    }

    uint64_t CreateJobId(nvs_handle_t handle)
    {
        for (uint8_t attempt = 0; attempt < 8; ++attempt)
        {
            const uint64_t candidate = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
            if (candidate == 0)
                continue;

            bool collision = false;
            for (uint8_t slot = 0; slot < NET_OUTBOX_CAPACITY; ++slot)
            {
                StoredNetworkJob record = {};
                if (ReadSlot(handle, slot, &record) == ESP_OK && record.job_id == candidate)
                {
                    collision = true;
                    break;
                }
            }
            if (!collision)
                return candidate;
        }
        return 0;
    }
}

bool NetOutbox_Init()
{
    s_storageReady = false;
    if (!s_mutex)
        s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex)
    {
        Serial.println("[联网待办] 无法创建存储互斥锁。");
        return false;
    }

    ScopedLock lock;
    if (!lock.locked() || !PrepareOutboxPartition())
        return false;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK)
    {
        Serial.printf("[联网待办] 无法打开 jobs 命名空间：%s。\n", esp_err_to_name(error));
        return false;
    }

    uint8_t pending = 0;
    uint8_t dead = 0;
    for (uint8_t slot = 0; slot < NET_OUTBOX_CAPACITY; ++slot)
    {
        StoredNetworkJob record = {};
        error = ReadSlot(handle, slot, &record);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK)
        {
            nvs_close(handle);
            Serial.printf("[联网待办] 槽位 %u 损坏或版本不兼容，已锁定 Outbox 以保留现场。\n",
                          static_cast<unsigned>(slot));
            return false;
        }
        if (record.state == static_cast<uint8_t>(NetOutboxJobState::Pending))
            ++pending;
        else
            ++dead;
    }
    nvs_close(handle);

    s_storageReady = true;
    Serial.printf("[联网待办] 专用存储已就绪：待处理=%u，死信=%u，容量=%u。\n",
                  static_cast<unsigned>(pending),
                  static_cast<unsigned>(dead),
                  static_cast<unsigned>(NET_OUTBOX_CAPACITY));
    return true;
}

NetOutboxStats NetOutbox_GetStats()
{
    NetOutboxStats stats;
    stats.storage_ready = s_storageReady;
    if (!s_storageReady)
        return stats;

    ScopedLock lock;
    if (!lock.locked())
    {
        stats.storage_ready = false;
        return stats;
    }

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READONLY, &handle) != ESP_OK)
    {
        stats.storage_ready = false;
        return stats;
    }

    for (uint8_t slot = 0; slot < NET_OUTBOX_CAPACITY; ++slot)
    {
        StoredNetworkJob record = {};
        const esp_err_t error = ReadSlot(handle, slot, &record);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK)
        {
            stats.storage_ready = false;
            break;
        }
        if (record.state == static_cast<uint8_t>(NetOutboxJobState::Pending))
            ++stats.pending_count;
        else
            ++stats.dead_letter_count;
    }
    nvs_close(handle);
    return stats;
}

NetOutboxEnqueueResult NetOutbox_Enqueue(
    uint16_t task_id,
    const void *payload,
    uint16_t payload_length,
    uint8_t priority,
    uint64_t dedup_key,
    uint64_t *out_job_id)
{
    if (out_job_id)
        *out_job_id = 0;
    if (task_id == 0 || payload_length > NET_OUTBOX_PAYLOAD_MAX || (payload_length > 0 && !payload))
        return NetOutboxEnqueueResult::InvalidArgument;
    if (!s_storageReady)
        return NetOutboxEnqueueResult::StorageUnavailable;

    ScopedLock lock;
    if (!lock.locked())
        return NetOutboxEnqueueResult::StorageUnavailable;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK)
        return NetOutboxEnqueueResult::StorageUnavailable;

    int free_slot = -1;
    for (uint8_t slot = 0; slot < NET_OUTBOX_CAPACITY; ++slot)
    {
        StoredNetworkJob existing = {};
        error = ReadSlot(handle, slot, &existing);
        if (error == ESP_ERR_NVS_NOT_FOUND)
        {
            if (free_slot < 0)
                free_slot = slot;
            continue;
        }
        if (error != ESP_OK)
        {
            nvs_close(handle);
            return NetOutboxEnqueueResult::StorageUnavailable;
        }
        if (dedup_key != 0 && existing.state == static_cast<uint8_t>(NetOutboxJobState::Pending) &&
            existing.task_id == task_id && existing.dedup_key == dedup_key)
        {
            if (out_job_id)
                *out_job_id = existing.job_id;
            nvs_close(handle);
            return NetOutboxEnqueueResult::AlreadyPending;
        }
    }

    if (free_slot < 0)
    {
        nvs_close(handle);
        return NetOutboxEnqueueResult::QueueFull;
    }

    uint32_t sequence = 0;
    error = nvs_get_u32(handle, kSequenceKey, &sequence);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return NetOutboxEnqueueResult::StorageUnavailable;
    }
    ++sequence;
    if (sequence == 0)
        sequence = 1;

    StoredNetworkJob pending = {};
    pending.magic = kRecordMagic;
    pending.schema_version = kSchemaVersion;
    pending.record_size = sizeof(StoredNetworkJob);
    pending.job_id = CreateJobId(handle);
    pending.created_sequence = sequence;
    pending.task_id = task_id;
    pending.priority = priority;
    pending.state = static_cast<uint8_t>(NetOutboxJobState::Pending);
    pending.payload_length = payload_length;
    pending.dedup_key = dedup_key;
    if (payload_length > 0)
        memcpy(pending.payload, payload, payload_length);

    if (pending.job_id == 0)
    {
        nvs_close(handle);
        return NetOutboxEnqueueResult::SaveFailed;
    }

    error = WriteSlot(handle, static_cast<uint8_t>(free_slot), &pending);
    if (error == ESP_OK)
        error = nvs_set_u32(handle, kSequenceKey, sequence);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);

    if (error != ESP_OK)
    {
        Serial.printf("[联网待办] 任务入队失败：%s。\n", esp_err_to_name(error));
        return NetOutboxEnqueueResult::SaveFailed;
    }

    if (out_job_id)
        *out_job_id = pending.job_id;
    Serial.printf("[联网待办] 已持久化任务：类型=%u，ID=%08lX%08lX。\n",
                  static_cast<unsigned>(task_id),
                  static_cast<unsigned long>(pending.job_id >> 32),
                  static_cast<unsigned long>(pending.job_id));
    return NetOutboxEnqueueResult::Ok;
}

bool NetOutbox_ClaimNextReady(int64_t now_epoch, NetOutboxJob *out_job)
{
    if (!s_storageReady || !out_job || now_epoch < kMinimumTrustedEpoch)
        return false;

    ScopedLock lock;
    if (!lock.locked())
        return false;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;

    int selected_slot = -1;
    StoredNetworkJob selected = {};
    for (uint8_t slot = 0; slot < NET_OUTBOX_CAPACITY; ++slot)
    {
        StoredNetworkJob candidate = {};
        const esp_err_t error = ReadSlot(handle, slot, &candidate);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK)
        {
            nvs_close(handle);
            return false;
        }
        if (candidate.state != static_cast<uint8_t>(NetOutboxJobState::Pending) ||
            (candidate.not_before_epoch > 0 && candidate.not_before_epoch > now_epoch))
            continue;

        if (selected_slot < 0 || candidate.priority > selected.priority ||
            (candidate.priority == selected.priority && candidate.created_sequence < selected.created_sequence))
        {
            selected_slot = slot;
            selected = candidate;
        }
    }

    if (selected_slot < 0)
    {
        nvs_close(handle);
        return false;
    }

    if (selected.attempt_count < UINT8_MAX)
        ++selected.attempt_count;
    esp_err_t error = WriteSlot(handle, static_cast<uint8_t>(selected_slot), &selected);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK)
    {
        Serial.printf("[联网待办] 无法提交领取状态：%s。\n", esp_err_to_name(error));
        return false;
    }

    CopyPublicJob(selected, out_job);
    return true;
}

bool NetOutbox_Complete(uint64_t job_id)
{
    if (!s_storageReady || job_id == 0)
        return false;
    ScopedLock lock;
    if (!lock.locked())
        return false;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;

    uint8_t slot = 0;
    StoredNetworkJob record = {};
    if (!FindJobSlot(handle, job_id, &slot, &record))
    {
        nvs_close(handle);
        return false;
    }

    char key[8] = {};
    BuildSlotKey(slot, key, sizeof(key));
    esp_err_t error = nvs_erase_key(handle, key);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

bool NetOutbox_Retry(uint64_t job_id, int64_t not_before_epoch)
{
    if (!s_storageReady || job_id == 0 || not_before_epoch < 0)
        return false;
    ScopedLock lock;
    if (!lock.locked())
        return false;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;
    uint8_t slot = 0;
    StoredNetworkJob record = {};
    if (!FindJobSlot(handle, job_id, &slot, &record))
    {
        nvs_close(handle);
        return false;
    }

    record.state = static_cast<uint8_t>(NetOutboxJobState::Pending);
    record.not_before_epoch = not_before_epoch;
    esp_err_t error = WriteSlot(handle, slot, &record);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}

bool NetOutbox_DeadLetter(uint64_t job_id)
{
    if (!s_storageReady || job_id == 0)
        return false;
    ScopedLock lock;
    if (!lock.locked())
        return false;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;
    uint8_t slot = 0;
    StoredNetworkJob record = {};
    if (!FindJobSlot(handle, job_id, &slot, &record))
    {
        nvs_close(handle);
        return false;
    }

    record.state = static_cast<uint8_t>(NetOutboxJobState::DeadLetter);
    esp_err_t error = WriteSlot(handle, slot, &record);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}
