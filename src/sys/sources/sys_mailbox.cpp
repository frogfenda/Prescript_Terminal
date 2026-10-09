/*
【模块职责】实现广播/私人双信箱、独立接收游标以及普通/日期文本的可靠主循环分发。
【存储布局】两路共用固定槽位，消息带渠道与持久接收顺序；旧版槽位无损迁移为私信。
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
#include "sys/sys_time.h"

namespace
{
    constexpr char kPartitionLabel[] = "mailbox";
    constexpr char kNamespace[] = "inbox";
    constexpr const char *kAcceptedSequenceKeys[] = {"private_ack", "broadcast_ack"};
    constexpr char kLegacyAcceptedSequenceKey[] = "accepted";
    constexpr char kMigrationNamespace[] = "sys_migrate";
    constexpr char kMigrationKey[] = "mailbox_v1";
    constexpr uint32_t kRecordMagic = 0x50544D42UL; // "PTMB"
    constexpr uint16_t kSchemaVersion = 2;

    struct __attribute__((packed)) StoredMailboxMessage
    {
        uint32_t magic;
        uint16_t schema_version;
        uint16_t record_size;
        uint64_t sequence;
        uint8_t type;
        uint8_t mailbox;
        uint16_t font_color;
        int64_t trigger_epoch;
        char message_id[37];
        char sender_public_id[65];
        char sender_alias[33];
        char content[SYS_MAILBOX_CONTENT_MAX + 1];
        uint64_t receive_order;
        uint32_t checksum;
    };

    static_assert(sizeof(StoredMailboxMessage) == 560,
                  "Mailbox 记录布局变化时必须提升 schema_version");
    constexpr size_t kLegacyRecordSize = 552;
    constexpr size_t kLegacyChecksumOffset = 548;

    SemaphoreHandle_t s_mutex = nullptr;
    portMUX_TYPE s_cursorMux = portMUX_INITIALIZER_UNLOCKED;
    uint64_t s_acceptedSequence[2] = {};
    uint64_t s_receiveOrder = 0;
    bool s_storageReady = false;
    uint64_t s_queuedSequence = 0;
    SysMailboxKind s_queuedMailbox = SysMailboxKind::Private;
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

    bool IsMailboxValid(SysMailboxKind mailbox)
    {
        return mailbox == SysMailboxKind::Private || mailbox == SysMailboxKind::Broadcast;
    }

    uint32_t CalculateBytesChecksum(const uint8_t *bytes, size_t length)
    {
        uint32_t hash = 2166136261UL;
        for (size_t index = 0; index < length; ++index)
        {
            hash ^= bytes[index];
            hash *= 16777619UL;
        }
        return hash;
    }

    uint32_t CalculateChecksum(const StoredMailboxMessage &record)
    {
        return CalculateBytesChecksum(reinterpret_cast<const uint8_t *>(&record),
                                      offsetof(StoredMailboxMessage, checksum));
    }

    /** 同时刻广播先于私信；各渠道序号彼此独立，只能在渠道相同时比较。 */
    bool ChannelSequenceBefore(const StoredMailboxMessage &left, const StoredMailboxMessage &right)
    {
        if (left.mailbox != right.mailbox)
            return left.mailbox == static_cast<uint8_t>(SysMailboxKind::Broadcast);
        return left.sequence < right.sequence;
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
               IsMailboxValid(static_cast<SysMailboxKind>(record.mailbox)) && record.receive_order > 0 &&
               record.type <= static_cast<uint8_t>(SysMailboxMessageType::ScheduledText) &&
               (record.type != static_cast<uint8_t>(SysMailboxMessageType::ScheduledText) || record.trigger_epoch > 0) &&
               IsNullTerminated(record.message_id, sizeof(record.message_id)) &&
               IsNullTerminated(record.sender_public_id, sizeof(record.sender_public_id)) &&
               IsNullTerminated(record.sender_alias, sizeof(record.sender_alias)) &&
               IsNullTerminated(record.content, sizeof(record.content)) &&
               record.message_id[0] != '\0' && record.sender_public_id[0] != '\0' && record.content[0] != '\0' &&
               record.checksum == CalculateChecksum(record);
    }

    esp_err_t ReadSlot(nvs_handle_t handle, uint8_t slot, StoredMailboxMessage *record,
                       bool *was_legacy = nullptr)
    {
        if (was_legacy)
            *was_legacy = false;
        char key[8] = {};
        BuildSlotKey(slot, key, sizeof(key));
        size_t length = sizeof(*record);
        const esp_err_t error = nvs_get_blob(handle, key, record, &length);
        /*
         * V1 的前 548 字节与 V2 一致（原 reserved 改作 mailbox）；先验证旧校验和再转换。
         * Init 会把转换结果持久化。中途掉电时，已经迁移的 V2 与尚未迁移的 V1 都可重读。
         */
        if (error == ESP_OK && length == kLegacyRecordSize && record->schema_version == 1)
        {
            uint32_t legacy_checksum = 0;
            memcpy(&legacy_checksum, reinterpret_cast<const uint8_t *>(record) + kLegacyChecksumOffset,
                   sizeof(legacy_checksum));
            if (record->record_size != kLegacyRecordSize || legacy_checksum !=
                CalculateBytesChecksum(reinterpret_cast<const uint8_t *>(record), kLegacyChecksumOffset))
                return ESP_ERR_INVALID_STATE;
            record->schema_version = kSchemaVersion;
            record->record_size = sizeof(*record);
            record->mailbox = static_cast<uint8_t>(SysMailboxKind::Private);
            record->receive_order = record->sequence;
            record->checksum = CalculateChecksum(*record);
            length = sizeof(*record);
            if (was_legacy)
                *was_legacy = true;
        }
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

    bool RemoveSequence(SysMailboxKind mailbox, uint64_t sequence)
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
            if (record.sequence != sequence || record.mailbox != static_cast<uint8_t>(mailbox))
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
            if (!found || candidate.receive_order < selected.receive_order ||
                (candidate.receive_order == selected.receive_order && ChannelSequenceBefore(candidate, selected)))
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
                (candidate.trigger_epoch == selected.trigger_epoch && ChannelSequenceBefore(candidate, selected)))
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
    uint64_t accepted[2] = {};
    for (size_t channel = 0; channel < 2; ++channel)
    {
        esp_err_t cursor_error = nvs_get_u64(handle, kAcceptedSequenceKeys[channel], &accepted[channel]);
        if (channel == 0 && cursor_error == ESP_ERR_NVS_NOT_FOUND)
            cursor_error = nvs_get_u64(handle, kLegacyAcceptedSequenceKey, &accepted[channel]);
        if (cursor_error != ESP_OK && cursor_error != ESP_ERR_NVS_NOT_FOUND)
        {
            nvs_close(handle);
            return false;
        }
    }
    /* 先校验全部槽，再迁移；损坏的数据保留现场，绝不通过清空信箱来完成升级。 */
    uint64_t receive_order = 0;
    for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
    {
        StoredMailboxMessage record = {};
        const esp_err_t error = ReadSlot(handle, slot, &record);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK)
        {
            nvs_close(handle);
            Serial.println("[设备信箱] 槽位校验失败，保留旧数据并暂停升级。");
            return false;
        }
        if (record.receive_order > receive_order)
            receive_order = record.receive_order;
        /* NVS 不提供多键事务；掉电在写槽与写游标之间时，用已校验的持久记录恢复游标。 */
        if (record.sequence > accepted[record.mailbox])
            accepted[record.mailbox] = record.sequence;
    }
    for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
    {
        StoredMailboxMessage record = {};
        bool legacy = false;
        const esp_err_t error = ReadSlot(handle, slot, &record, &legacy);
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND)
        {
            nvs_close(handle);
            return false;
        }
        if (legacy && WriteSlot(handle, slot, &record) != ESP_OK)
        {
            nvs_close(handle);
            return false;
        }
    }
    if (nvs_set_u64(handle, kAcceptedSequenceKeys[0], accepted[0]) != ESP_OK ||
        nvs_set_u64(handle, kAcceptedSequenceKeys[1], accepted[1]) != ESP_OK ||
        nvs_commit(handle) != ESP_OK)
    {
        nvs_close(handle);
        return false;
    }
    // 新游标已提交后才移除旧键；失败只留下无害旧键，重启优先读取新键。
    if (nvs_erase_key(handle, kLegacyAcceptedSequenceKey) == ESP_OK)
        nvs_commit(handle);
    if (!RebuildPendingSummary(handle))
    {
        nvs_close(handle);
        Serial.println("[设备信箱] 存在损坏或版本不兼容的槽位，已锁定收件箱。");
        return false;
    }
    nvs_close(handle);

    portENTER_CRITICAL(&s_cursorMux);
    s_acceptedSequence[0] = accepted[0];
    s_acceptedSequence[1] = accepted[1];
    portEXIT_CRITICAL(&s_cursorMux);
    s_storageReady = true;
    s_queuedSequence = 0;
    s_receiveOrder = receive_order;
    Serial.printf("[设备信箱] 双信箱已就绪：本地待处理=%u，私人序号=%llu，广播序号=%llu，容量=%u。\n",
                  static_cast<unsigned>(s_instantPendingCount + s_scheduledPendingCount),
                  static_cast<unsigned long long>(accepted[0]),
                  static_cast<unsigned long long>(accepted[1]),
                  static_cast<unsigned>(SYS_MAILBOX_CAPACITY));
    return true;
}

SysMailboxAcceptResult SysMailbox_Accept(const SysMailboxIncomingMessage &message)
{
    const size_t content_length = strnlen(message.content, sizeof(message.content));
    if (!s_storageReady || !s_mutex)
        return SysMailboxAcceptResult::StorageUnavailable;
    if (!IsMailboxValid(message.mailbox) || message.sequence == 0 ||
        message.type > SysMailboxMessageType::ScheduledText ||
        !IsNullTerminated(message.message_id, sizeof(message.message_id)) ||
        !IsNullTerminated(message.sender_public_id, sizeof(message.sender_public_id)) ||
        !IsNullTerminated(message.sender_alias, sizeof(message.sender_alias)) ||
        message.message_id[0] == '\0' || message.sender_public_id[0] == '\0' ||
        content_length == 0 || content_length > SYS_MAILBOX_CONTENT_MAX ||
        (message.type == SysMailboxMessageType::ScheduledText && message.trigger_epoch <= 0))
        return SysMailboxAcceptResult::InvalidMessage;

    ScopedLock lock;
    if (!lock.locked())
        return SysMailboxAcceptResult::StorageUnavailable;
    const size_t channel = static_cast<size_t>(message.mailbox);
    if (message.sequence <= s_acceptedSequence[channel])
        return SysMailboxAcceptResult::AlreadyAccepted;
    if (message.mailbox == SysMailboxKind::Private && message.sequence != s_acceptedSequence[channel] + 1)
        return SysMailboxAcceptResult::SequenceGap;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return SysMailboxAcceptResult::StorageUnavailable;
    int free_slot = -1;
    int existing_slot = -1;
    uint8_t channel_count = 0;
    uint64_t existing_order = 0;
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
        if (existing.mailbox == static_cast<uint8_t>(message.mailbox))
        {
            ++channel_count;
            if (existing.sequence == message.sequence)
            {
                // 上次游标提交失败时修复同一槽位，不能为同一封消息再占一个槽。
                if (strcmp(existing.message_id, message.message_id) != 0)
                {
                    nvs_close(handle);
                    return SysMailboxAcceptResult::InvalidMessage;
                }
                existing_slot = slot;
                existing_order = existing.receive_order;
            }
        }
    }
    if (existing_slot < 0 && (free_slot < 0 || channel_count >= SYS_MAILBOX_CHANNEL_CAPACITY))
    {
        nvs_close(handle);
        return SysMailboxAcceptResult::QueueFull;
    }
    if (existing_slot < 0 && s_receiveOrder == UINT64_MAX)
    {
        nvs_close(handle);
        return SysMailboxAcceptResult::StorageUnavailable;
    }

    StoredMailboxMessage record = {};
    record.magic = kRecordMagic;
    record.schema_version = kSchemaVersion;
    record.record_size = sizeof(StoredMailboxMessage);
    record.sequence = message.sequence;
    record.type = static_cast<uint8_t>(message.type);
    record.mailbox = static_cast<uint8_t>(message.mailbox);
    record.receive_order = existing_slot >= 0 ? existing_order : s_receiveOrder + 1;
    record.font_color = message.font_color;
    record.trigger_epoch = message.trigger_epoch;
    strlcpy(record.message_id, message.message_id, sizeof(record.message_id));
    strlcpy(record.sender_public_id, message.sender_public_id, sizeof(record.sender_public_id));
    strlcpy(record.sender_alias, message.sender_alias, sizeof(record.sender_alias));
    strlcpy(record.content, message.content, sizeof(record.content));

    esp_err_t error = WriteSlot(handle, static_cast<uint8_t>(existing_slot >= 0 ? existing_slot : free_slot), &record);
    if (error == ESP_OK)
    {
        // 写槽成功即预留此接收顺序；即使后续游标失败，另一渠道也不能复用同一个顺序值。
        if (record.receive_order > s_receiveOrder)
            s_receiveOrder = record.receive_order;
        error = nvs_set_u64(handle, kAcceptedSequenceKeys[channel], message.sequence);
    }
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
    s_acceptedSequence[channel] = message.sequence;
    portEXIT_CRITICAL(&s_cursorMux);
    if (record.receive_order > s_receiveOrder)
        s_receiveOrder = record.receive_order;
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

uint64_t SysMailbox_GetAcceptedSequence(SysMailboxKind mailbox)
{
    if (!IsMailboxValid(mailbox))
        return 0;
    portENTER_CRITICAL(&s_cursorMux);
    const uint64_t value = s_acceptedSequence[static_cast<size_t>(mailbox)];
    portEXIT_CRITICAL(&s_cursorMux);
    return value;
}

bool SysMailbox_ReconcileServerAcknowledged(SysMailboxKind mailbox, uint64_t server_sequence)
{
    if (!IsMailboxValid(mailbox))
        return false;
    if (!s_storageReady || server_sequence == 0)
        return s_storageReady;
    ScopedLock lock;
    if (!lock.locked())
        return false;
    const size_t channel = static_cast<size_t>(mailbox);
    if (server_sequence <= s_acceptedSequence[channel])
        return true;

    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;

    /*
     * 账号下的另一客户端也可能推进服务端游标，并且服务器不会再次返回这些消息。因此服务端游标
     * 领先时，继续停在旧本地游标只会让下一封消息永久触发 SequenceGap。这里先完整校验现有
     * 槽位仍属于旧本地连续区间，然后仅推进接收基线；槽位本身继续等待提醒系统正常展示和删除。
     */
    for (uint8_t slot = 0; slot < SYS_MAILBOX_CAPACITY; ++slot)
    {
        StoredMailboxMessage record = {};
        const esp_err_t error = ReadSlot(handle, slot, &record);
        if (error == ESP_ERR_NVS_NOT_FOUND)
            continue;
        if (error != ESP_OK ||
            (record.mailbox == static_cast<uint8_t>(mailbox) && record.sequence > s_acceptedSequence[channel]))
        {
            nvs_close(handle);
            return false;
        }
    }
    esp_err_t error = nvs_set_u64(handle, kAcceptedSequenceKeys[channel], server_sequence);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK)
        return false;

    portENTER_CRITICAL(&s_cursorMux);
    s_acceptedSequence[channel] = server_sequence;
    portEXIT_CRITICAL(&s_cursorMux);
    Serial.printf("[设备信箱] 已对齐%s确认游标：本地待处理=%u，后续接收基线=%llu。\n",
                  mailbox == SysMailboxKind::Broadcast ? "广播" : "私人",
                  static_cast<unsigned>(s_instantPendingCount + s_scheduledPendingCount),
                  static_cast<unsigned long long>(server_sequence));
    return true;
}

void SysMailbox_Update()
{
    if (!s_storageReady || s_queuedSequence != 0)
        return;
    /*
     * 即使日历本轮尚未达到 250ms 检查间隔，也先检查远程定时消息，避免即时积压抢占门闩。
     * 时间未可信时只处理即时消息；日期消息仍由统一日历与 RTC 维护，绝不拿无效时间触发。
     */
    SysTimeSnapshot snapshot = {};
    if (SysTime_GetSnapshot(&snapshot) && snapshot.valid && SysMailbox_HasDueScheduled(snapshot.epoch))
    {
        SysMailbox_ProcessDueScheduled(snapshot.epoch);
        return;
    }
    if (s_instantPendingCount == 0)
        return;
    StoredMailboxMessage message = {};
    if (!FindNextInstant(message))
        return;
    if (SysReminder_Submit(
            SysReminderKind::Custom,
            message.content,
            false,
            message.font_color,
            message.sequence,
            static_cast<SysMailboxKind>(message.mailbox)))
    {
        s_queuedSequence = message.sequence;
        s_queuedMailbox = static_cast<SysMailboxKind>(message.mailbox);
    }
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
        output[count].mailbox = static_cast<SysMailboxKind>(record.mailbox);
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
            message.sequence,
            static_cast<SysMailboxKind>(message.mailbox)))
        return false;
    s_queuedSequence = message.sequence;
    s_queuedMailbox = static_cast<SysMailboxKind>(message.mailbox);
    SysCalendar_NotifyDataChanged();
    return true;
}

void SysMailbox_ConfirmDispatched(SysMailboxKind mailbox, uint64_t sequence)
{
    if (!s_storageReady || sequence == 0 || sequence != s_queuedSequence || mailbox != s_queuedMailbox)
        return;
    if (!RemoveSequence(mailbox, sequence))
    {
        Serial.printf("[设备信箱] 消息序号 %llu 已触发，但本地记录删除失败；下次可能重复展示。\n",
                      static_cast<unsigned long long>(sequence));
        s_queuedSequence = 0;
        return;
    }
    s_queuedSequence = 0;
    SysCalendar_NotifyDataChanged();
}
