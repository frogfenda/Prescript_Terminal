/*
【模块职责】实现独立 NVS 收件箱、连续接收游标以及普通/日期文本的可靠主循环分发。
【存储布局】每封消息占一个固定槽；正文和发送者公开身份随记录一起校验，便于以后增加信箱 UI。
*/
#include "sys/sys_mailbox.h"

#include <cstddef>
#include <cstring>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "sys/sys_calendar.h"
#include "sys/sys_reminder.h"

namespace
{
    constexpr char kPartitionLabel[] = "mailbox";
    constexpr char kNamespace[] = "inbox";
    constexpr char kAcceptedSequenceKey[] = "accepted";
    constexpr char kMigrationNamespace[] = "sys_migrate";
    constexpr char kMigrationKey[] = "mailbox_v1";
    constexpr uint32_t kRecordMagic = 0x50544D42UL; // "PTMB"
    constexpr uint16_t kSchemaVersion = 1;

    struct __attribute__((packed)) StoredMailboxMessage
    {
        uint32_t magic;
        uint16_t schema_version;
        uint16_t record_size;
        uint64_t sequence;
        uint8_t type;
        uint8_t reserved;
        uint16_t font_color;
        int64_t trigger_epoch;
        char message_id[37];
        char sender_public_id[65];
        char sender_alias[33];
        char content[SYS_MAILBOX_CONTENT_MAX + 1];
        uint32_t checksum;
    };

    static_assert(sizeof(StoredMailboxMessage) == 552,
                  "Mailbox 记录布局变化时必须提升 schema_version");

    SemaphoreHandle_t s_mutex = nullptr;
    portMUX_TYPE s_cursorMux = portMUX_INITIALIZER_UNLOCKED;
    uint64_t s_acceptedSequence = 0;
    bool s_storageReady = false;
    uint64_t s_queuedSequence = 0;
    uint8_t s_instantPendingCount = 0;
    uint8_t s_scheduledPendingCount = 0;
    int64_t s_nextScheduledEpoch = 0;

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

    uint32_t CalculateChecksum(const StoredMailboxMessage &record)
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
        const size_t length = offsetof(StoredMailboxMessage, checksum);
        uint32_t hash = 2166136261UL;
        for (size_t index = 0; index < length; ++index)
        {
            hash ^= bytes[index];
            hash *= 16777619UL;
        }
        return hash;
    }

    void BuildSlotKey(uint8_t slot, char *output, size_t output_size)
    {
        snprintf(output, output_size, "msg%02u", static_cast<unsigned>(slot));
    }

    bool IsNullTerminated(const char *value, size_t capacity)
    {
        return value && memchr(value, '\0', capacity) != nullptr;
    }

    bool IsRecordValid(const StoredMailboxMessage &record)
    {
        return record.magic == kRecordMagic &&
               record.schema_version == kSchemaVersion &&
               record.record_size == sizeof(StoredMailboxMessage) &&
               record.sequence > 0 &&
               record.type <= static_cast<uint8_t>(SysMailboxMessageType::ScheduledText) &&
               (record.type != static_cast<uint8_t>(SysMailboxMessageType::ScheduledText) || record.trigger_epoch > 0) &&
               IsNullTerminated(record.message_id, sizeof(record.message_id)) &&
               IsNullTerminated(record.sender_public_id, sizeof(record.sender_public_id)) &&
               IsNullTerminated(record.sender_alias, sizeof(record.sender_alias)) &&
               IsNullTerminated(record.content, sizeof(record.content)) &&
               record.message_id[0] != '\0' && record.sender_public_id[0] != '\0' && record.content[0] != '\0' &&
               record.checksum == CalculateChecksum(record);
    }

    esp_err_t ReadSlot(nvs_handle_t handle, uint8_t slot, StoredMailboxMessage *record)
    {
        char key[8] = {};
        BuildSlotKey(slot, key, sizeof(key));
        size_t length = sizeof(*record);
        const esp_err_t error = nvs_get_blob(handle, key, record, &length);
        if (error == ESP_OK && (length != sizeof(*record) || !IsRecordValid(*record)))
            return ESP_ERR_INVALID_STATE;
        return error;
    }

    esp_err_t WriteSlot(nvs_handle_t handle, uint8_t slot, StoredMailboxMessage *record)
    {
        record->checksum = CalculateChecksum(*record);
        char key[8] = {};
        BuildSlotKey(slot, key, sizeof(key));
        return nvs_set_blob(handle, key, record, sizeof(*record));
    }

    /** 只在初始化或删除成功后重建轻量摘要，避免空信箱在每轮主循环扫描 NVS。 */
    bool RebuildPendingSummary(nvs_handle_t handle)
    {
        uint8_t instant_count = 0;
        uint8_t scheduled_count = 0;
        int64_t next_scheduled = 0;
        for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
        {
            StoredMailboxMessage record = {};
            const esp_err_t error = ReadSlot(handle, slot, &record);
            if (error == ESP_ERR_NVS_NOT_FOUND)
                continue;
            if (error != ESP_OK)
                return false;
            if (record.type == static_cast<uint8_t>(SysMailboxMessageType::InstantText))
            {
                ++instant_count;
            }
            else
            {
                ++scheduled_count;
                if (next_scheduled == 0 || record.trigger_epoch < next_scheduled)
                    next_scheduled = record.trigger_epoch;
            }
        }
        s_instantPendingCount = instant_count;
        s_scheduledPendingCount = scheduled_count;
        s_nextScheduledEpoch = next_scheduled;
        return true;
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

    bool PreparePartition()
    {
        const bool initialized_before = WasPartitionInitializedBefore();
        esp_err_t error = nvs_flash_init_partition(kPartitionLabel);
        if ((error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) &&
            !initialized_before)
        {
            Serial.println("[设备信箱] 首次启用专用分区，只格式化 mailbox 区域。");
            error = nvs_flash_erase_partition(kPartitionLabel);
            if (error == ESP_OK)
                error = nvs_flash_init_partition(kPartitionLabel);
        }
        if (error != ESP_OK)
        {
            Serial.printf("[设备信箱] mailbox 分区初始化失败：%s；未自动擦除已有收件。\n",
                          esp_err_to_name(error));
            return false;
        }
        if (!initialized_before && !SavePartitionInitializedMarker())
        {
            Serial.println("[设备信箱] 无法保存首次初始化标记，拒绝启用收件存储。");
            return false;
        }
        return true;
    }

    bool RemoveSequence(uint64_t sequence)
    {
        ScopedLock lock;
        if (!lock.locked())
            return false;
        nvs_handle_t handle = 0;
        if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return false;
        for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
        {
            StoredMailboxMessage record = {};
            const esp_err_t read_error = ReadSlot(handle, slot, &record);
            if (read_error == ESP_ERR_NVS_NOT_FOUND)
                continue;
            if (read_error != ESP_OK)
            {
                nvs_close(handle);
                return false;
            }
            if (record.sequence != sequence)
                continue;
            char key[8] = {};
            BuildSlotKey(slot, key, sizeof(key));
            esp_err_t error = nvs_erase_key(handle, key);
            if (error == ESP_OK)
                error = nvs_commit(handle);
            if (error == ESP_OK && !RebuildPendingSummary(handle))
                error = ESP_ERR_INVALID_STATE;
            nvs_close(handle);
            return error == ESP_OK;
        }
        nvs_close(handle);
        return false;
    }

    bool FindNextInstant(StoredMailboxMessage &selected)
    {
        ScopedLock lock;
        if (!lock.locked())
            return false;
        nvs_handle_t handle = 0;
        if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READONLY, &handle) != ESP_OK)
            return false;
        bool found = false;
        for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
        {
            StoredMailboxMessage candidate = {};
            const esp_err_t error = ReadSlot(handle, slot, &candidate);
            if (error == ESP_ERR_NVS_NOT_FOUND)
                continue;
            if (error != ESP_OK)
            {
                found = false;
                break;
            }
            if (candidate.type != static_cast<uint8_t>(SysMailboxMessageType::InstantText))
                continue;
            if (!found || candidate.sequence < selected.sequence)
            {
                selected = candidate;
                found = true;
            }
        }
        nvs_close(handle);
        return found;
    }

    bool FindNextDueScheduled(time_t now, StoredMailboxMessage &selected)
    {
        ScopedLock lock;
        if (!lock.locked())
            return false;
        nvs_handle_t handle = 0;
        if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READONLY, &handle) != ESP_OK)
            return false;
        bool found = false;
        for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
        {
            StoredMailboxMessage candidate = {};
            const esp_err_t error = ReadSlot(handle, slot, &candidate);
            if (error == ESP_ERR_NVS_NOT_FOUND)
                continue;
            if (error != ESP_OK)
            {
                found = false;
                break;
            }
            if (candidate.type != static_cast<uint8_t>(SysMailboxMessageType::ScheduledText) ||
                candidate.trigger_epoch > static_cast<int64_t>(now))
                continue;
            if (!found || candidate.trigger_epoch < selected.trigger_epoch ||
                (candidate.trigger_epoch == selected.trigger_epoch && candidate.sequence < selected.sequence))
            {
                selected = candidate;
                found = true;
            }
        }
        nvs_close(handle);
        return found;
    }
}

bool SysMailbox_Init()
{
    s_storageReady = false;
    if (!s_mutex)
        s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex || !PreparePartition())
        return false;

    ScopedLock lock;
    if (!lock.locked())
        return false;
    nvs_handle_t handle = 0;
    /*
     * 全新 NVS 分区中尚不存在 inbox 命名空间。NVS_READONLY 只能打开既有命名空间，不能
     * 创建它，因此首次启动必须以读写方式打开；这里只建立命名空间，后续扫描仍不会写消息。
     */
    const esp_err_t open_error =
        nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle);
    if (open_error != ESP_OK)
    {
        Serial.printf("[设备信箱] inbox 命名空间打开失败：%s。\n", esp_err_to_name(open_error));
        return false;
    }
    uint64_t accepted = 0;
    const esp_err_t cursor_error = nvs_get_u64(handle, kAcceptedSequenceKey, &accepted);
    if (cursor_error != ESP_OK && cursor_error != ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return false;
    }
    if (!RebuildPendingSummary(handle))
    {
        nvs_close(handle);
        Serial.println("[设备信箱] 存在损坏或版本不兼容的槽位，已锁定收件箱。");
        return false;
    }
    nvs_close(handle);

    portENTER_CRITICAL(&s_cursorMux);
    s_acceptedSequence = accepted;
    portEXIT_CRITICAL(&s_cursorMux);
    s_storageReady = true;
    s_queuedSequence = 0;
    Serial.printf("[设备信箱] 专用存储已就绪：本地待处理=%u，连续接收序号=%llu，容量=%u。\n",
                  static_cast<unsigned>(s_instantPendingCount + s_scheduledPendingCount),
                  static_cast<unsigned long long>(accepted),
                  static_cast<unsigned>(SYS_MAILBOX_CAPACITY));
    return true;
}

SysMailboxAcceptResult SysMailbox_Accept(const SysMailboxIncomingMessage &message)
{
    const size_t content_length = strnlen(message.content, sizeof(message.content));
    if (!s_storageReady || !s_mutex)
        return SysMailboxAcceptResult::StorageUnavailable;
    if (message.sequence == 0 ||
        message.type > SysMailboxMessageType::ScheduledText ||
        message.message_id[0] == '\0' || message.sender_public_id[0] == '\0' ||
        content_length == 0 || content_length > SYS_MAILBOX_CONTENT_MAX ||
        (message.type == SysMailboxMessageType::ScheduledText && message.trigger_epoch <= 0))
        return SysMailboxAcceptResult::InvalidMessage;

    ScopedLock lock;
    if (!lock.locked())
        return SysMailboxAcceptResult::StorageUnavailable;
    if (message.sequence <= s_acceptedSequence)
        return SysMailboxAcceptResult::AlreadyAccepted;
    if (message.sequence != s_acceptedSequence + 1)
        return SysMailboxAcceptResult::SequenceGap;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return SysMailboxAcceptResult::StorageUnavailable;
    int free_slot = -1;
    for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
    {
        StoredMailboxMessage existing = {};
        const esp_err_t error = ReadSlot(handle, slot, &existing);
        if (error == ESP_ERR_NVS_NOT_FOUND)
        {
            if (free_slot < 0)
                free_slot = slot;
            continue;
        }
        if (error != ESP_OK)
        {
            nvs_close(handle);
            return SysMailboxAcceptResult::StorageUnavailable;
        }
    }
    if (free_slot < 0)
    {
        nvs_close(handle);
        return SysMailboxAcceptResult::QueueFull;
    }

    StoredMailboxMessage record = {};
    record.magic = kRecordMagic;
    record.schema_version = kSchemaVersion;
    record.record_size = sizeof(StoredMailboxMessage);
    record.sequence = message.sequence;
    record.type = static_cast<uint8_t>(message.type);
    record.font_color = message.font_color;
    record.trigger_epoch = message.trigger_epoch;
    strlcpy(record.message_id, message.message_id, sizeof(record.message_id));
    strlcpy(record.sender_public_id, message.sender_public_id, sizeof(record.sender_public_id));
    strlcpy(record.sender_alias, message.sender_alias, sizeof(record.sender_alias));
    strlcpy(record.content, message.content, sizeof(record.content));

    esp_err_t error = WriteSlot(handle, static_cast<uint8_t>(free_slot), &record);
    if (error == ESP_OK)
        error = nvs_set_u64(handle, kAcceptedSequenceKey, message.sequence);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK)
    {
        Serial.printf("[设备信箱] 消息序号 %llu 写入失败：%s。\n",
                      static_cast<unsigned long long>(message.sequence), esp_err_to_name(error));
        return SysMailboxAcceptResult::StorageUnavailable;
    }

    portENTER_CRITICAL(&s_cursorMux);
    s_acceptedSequence = message.sequence;
    portEXIT_CRITICAL(&s_cursorMux);
    if (message.type == SysMailboxMessageType::ScheduledText)
    {
        ++s_scheduledPendingCount;
        if (s_nextScheduledEpoch == 0 || message.trigger_epoch < s_nextScheduledEpoch)
            s_nextScheduledEpoch = message.trigger_epoch;
        SysCalendar_NotifyDataChanged();
    }
    else
    {
        ++s_instantPendingCount;
    }
    return SysMailboxAcceptResult::Accepted;
}

uint64_t SysMailbox_GetAcceptedSequence()
{
    portENTER_CRITICAL(&s_cursorMux);
    const uint64_t value = s_acceptedSequence;
    portEXIT_CRITICAL(&s_cursorMux);
    return value;
}

bool SysMailbox_ReconcileServerAcknowledged(uint64_t server_sequence)
{
    if (!s_storageReady || server_sequence == 0)
        return s_storageReady;
    ScopedLock lock;
    if (!lock.locked())
        return false;
    if (server_sequence <= s_acceptedSequence)
        return true;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;

    /*
     * 服务端只会推进已经由设备累计 ACK 的序号，并且不会再次返回这些消息。因此服务端游标
     * 领先时，继续停在旧本地游标只会让下一封消息永久触发 SequenceGap。这里先完整校验现有
     * 槽位仍属于旧本地连续区间，然后仅推进接收基线；槽位本身继续等待提醒系统正常展示和删除。
     */
    for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
    {
        StoredMailboxMessage record = {};
        const esp_err_t error = ReadSlot(handle, slot, &record);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK || record.sequence > s_acceptedSequence)
        {
            nvs_close(handle);
            return false;
        }
    }
    esp_err_t error = nvs_set_u64(handle, kAcceptedSequenceKey, server_sequence);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK)
        return false;

    portENTER_CRITICAL(&s_cursorMux);
    s_acceptedSequence = server_sequence;
    portEXIT_CRITICAL(&s_cursorMux);
    Serial.printf("[设备信箱] 已对齐服务端确认游标：本地待处理=%u，后续接收基线=%llu。\n",
                  static_cast<unsigned>(s_instantPendingCount + s_scheduledPendingCount),
                  static_cast<unsigned long long>(server_sequence));
    return true;
}

void SysMailbox_Update()
{
    if (!s_storageReady || s_queuedSequence != 0 || s_instantPendingCount == 0)
        return;
    StoredMailboxMessage message = {};
    if (!FindNextInstant(message))
        return;
    if (SysReminder_Submit(
            SysReminderKind::Custom,
            message.content,
            false,
            message.font_color,
            message.sequence))
        s_queuedSequence = message.sequence;
}

size_t SysMailbox_CopyScheduledEvents(SysMailboxScheduledEvent *output, size_t capacity)
{
    if (!s_storageReady || !output || capacity == 0)
        return 0;
    ScopedLock lock;
    if (!lock.locked())
        return 0;
    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READONLY, &handle) != ESP_OK)
        return 0;
    size_t count = 0;
    for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY && count < capacity; ++slot)
    {
        StoredMailboxMessage record = {};
        const esp_err_t error = ReadSlot(handle, slot, &record);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK)
        {
            count = 0;
            break;
        }
        if (record.type != static_cast<uint8_t>(SysMailboxMessageType::ScheduledText))
            continue;
        output[count].slot = slot;
        output[count].sequence = record.sequence;
        output[count].trigger_epoch = static_cast<time_t>(record.trigger_epoch);
        ++count;
    }
    nvs_close(handle);
    return count;
}

bool SysMailbox_HasDueScheduled(time_t now)
{
    StoredMailboxMessage message = {};
    return s_storageReady && s_scheduledPendingCount > 0 &&
           s_nextScheduledEpoch > 0 && s_nextScheduledEpoch <= static_cast<int64_t>(now) &&
           FindNextDueScheduled(now, message);
}

bool SysMailbox_ProcessDueScheduled(time_t now)
{
    if (!s_storageReady || s_queuedSequence != 0 || s_scheduledPendingCount == 0 ||
        s_nextScheduledEpoch <= 0 || s_nextScheduledEpoch > static_cast<int64_t>(now))
        return false;
    StoredMailboxMessage message = {};
    if (!FindNextDueScheduled(now, message))
        return false;
    if (!SysReminder_Submit(
            SysReminderKind::Custom,
            message.content,
            false,
            message.font_color,
            message.sequence))
        return false;
    s_queuedSequence = message.sequence;
    return true;
}

void SysMailbox_ConfirmDispatched(uint64_t sequence)
{
    if (!s_storageReady || sequence == 0 || sequence != s_queuedSequence)
        return;
    if (!RemoveSequence(sequence))
    {
        Serial.printf("[设备信箱] 消息序号 %llu 已触发，但本地记录删除失败；下次可能重复展示。\n",
                      static_cast<unsigned long long>(sequence));
        s_queuedSequence = 0;
        return;
    }
    s_queuedSequence = 0;
    SysCalendar_NotifyDataChanged();
}
