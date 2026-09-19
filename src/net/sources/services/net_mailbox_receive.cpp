/*
【模块职责】使用本轮设备 Token 先确认上次可靠落盘游标，再顺序拉取最多五封消息。
【并发边界】Core 0 只写固定大小交接区；main_update 在 Arduino 主循环调用 SysMailbox_Accept。
*/
#include "net/services/net_mailbox_receive.h"

#include <ArduinoJson.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <mutex>
#include "net/net_auth_session.h"
#include "net/net_task_registry.h"
#include "sys/sys_mailbox.h"

namespace
{
    constexpr size_t kPullResponseLimit = 6144;
    constexpr size_t kAckResponseLimit = 512;
    constexpr uint8_t kBatchCapacity = 5;

    struct HandoffBuffer
    {
        SysMailboxIncomingMessage items[kBatchCapacity];
        uint8_t head = 0;
        uint8_t count = 0;
        uint64_t server_acknowledged_sequence = 0;
        bool server_cursor_pending = false;
    };

    HandoffBuffer *s_handoff = nullptr;
    std::mutex s_handoffMutex;
    uint32_t s_nextMainUpdateRetryMs = 0;
    bool s_cursorFailureReported = false;
    bool s_messageFailureReported = false;
    SysMailboxAcceptResult s_lastMessageFailure = SysMailboxAcceptResult::Accepted;

    uint16_t ParseRgb888To565(const char *value, bool &valid)
    {
        valid = false;
        if (!value || strlen(value) != 7 || value[0] != '#')
            return 0x07FF;
        char *end = nullptr;
        const unsigned long rgb = strtoul(value + 1, &end, 16);
        if (!end || *end != '\0')
            return 0x07FF;
        const uint8_t red = static_cast<uint8_t>((rgb >> 16) & 0xFF);
        const uint8_t green = static_cast<uint8_t>((rgb >> 8) & 0xFF);
        const uint8_t blue = static_cast<uint8_t>(rgb & 0xFF);
        valid = true;
        return static_cast<uint16_t>(((red & 0xF8) << 8) |
                                     ((green & 0xFC) << 3) |
                                     (blue >> 3));
    }

    bool HandoffIsEmpty()
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        return s_handoff && s_handoff->count == 0 && !s_handoff->server_cursor_pending;
    }

    bool PushHandoff(const SysMailboxIncomingMessage &message)
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        if (!s_handoff || s_handoff->count >= kBatchCapacity)
            return false;
        const uint8_t tail = static_cast<uint8_t>((s_handoff->head + s_handoff->count) % kBatchCapacity);
        s_handoff->items[tail] = message;
        ++s_handoff->count;
        return true;
    }

    bool PeekHandoff(SysMailboxIncomingMessage &message)
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        if (!s_handoff || s_handoff->count == 0)
            return false;
        message = s_handoff->items[s_handoff->head];
        return true;
    }

    void DropHandoff()
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        if (!s_handoff || s_handoff->count == 0)
            return;
        memset(&s_handoff->items[s_handoff->head], 0, sizeof(SysMailboxIncomingMessage));
        s_handoff->head = static_cast<uint8_t>((s_handoff->head + 1) % kBatchCapacity);
        --s_handoff->count;
    }

    void SetServerCursorHandoff(uint64_t sequence)
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        s_handoff->server_acknowledged_sequence = sequence;
        s_handoff->server_cursor_pending = true;
    }

    bool PeekServerCursorHandoff(uint64_t &sequence)
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        if (!s_handoff || !s_handoff->server_cursor_pending)
            return false;
        sequence = s_handoff->server_acknowledged_sequence;
        return true;
    }

    void DropServerCursorHandoff()
    {
        std::lock_guard<std::mutex> lock(s_handoffMutex);
        if (s_handoff)
            s_handoff->server_cursor_pending = false;
    }

    bool ParseEnvelope(JsonObjectConst object, SysMailboxIncomingMessage &message)
    {
        memset(&message, 0, sizeof(message));
        const char *message_id = object["id"] | "";
        const char *sender_public_id = object["sender_public_id"] | "";
        const char *sender_alias = object["sender_alias"] | "";
        const char *type = object["type"] | "";
        const char *content = object["content"] | "";
        const char *font_color = object["font_color"] | "";
        const uint64_t sequence = object["sequence"] | static_cast<uint64_t>(0);

        bool color_valid = false;
        const uint16_t color = ParseRgb888To565(font_color, color_valid);
        if (sequence == 0 || strlen(message_id) != 36 || strlen(sender_public_id) == 0 ||
            strlen(sender_public_id) >= sizeof(message.sender_public_id) ||
            strlen(sender_alias) >= sizeof(message.sender_alias) || strlen(content) == 0 ||
            strlen(content) > SYS_MAILBOX_CONTENT_MAX || !color_valid)
            return false;

        if (strcmp(type, "instant_text") == 0)
        {
            message.type = SysMailboxMessageType::InstantText;
        }
        else if (strcmp(type, "scheduled_text") == 0)
        {
            message.type = SysMailboxMessageType::ScheduledText;
            message.trigger_epoch = object["trigger_epoch"] | static_cast<int64_t>(0);
            if (message.trigger_epoch <= 0)
                return false;
        }
        else
        {
            return false;
        }

        message.sequence = sequence;
        message.font_color = color;
        strlcpy(message.message_id, message_id, sizeof(message.message_id));
        strlcpy(message.sender_public_id, sender_public_id, sizeof(message.sender_public_id));
        strlcpy(message.sender_alias, sender_alias, sizeof(message.sender_alias));
        strlcpy(message.content, content, sizeof(message.content));
        return true;
    }

    NetTaskExecutionResult Execute(const NetTaskInvocation &, const NetTaskContext &context)
    {
        if (!s_handoff || !HandoffIsEmpty())
        {
            Serial.println("[设备收件] 上批消息尚未由主循环落盘，本轮暂不重复拉取。");
            return {NetTaskDisposition::Retry, 60};
        }

        const uint64_t accepted_sequence = SysMailbox_GetAcceptedSequence();
        if (accepted_sequence > 0)
        {
            JsonDocument ack_document;
            ack_document["through_sequence"] = accepted_sequence;
            String ack_body;
            serializeJson(ack_document, ack_body);
            NetHttpResponse ack_response;
            const NetHttpResult ack_transport = NetAuthSession_PostJson(
                "/api/v1/device/mailbox/ack",
                ack_body,
                context,
                kAckResponseLimit,
                ack_response);
            if (ack_transport != NetHttpResult::Ok || ack_response.status_code != 200)
            {
                Serial.printf("[设备收件] 上轮接收游标确认失败：传输=%u，HTTP=%d。\n",
                              static_cast<unsigned>(ack_transport), ack_response.status_code);
                return {NetTaskDisposition::Retry, 5UL * 60UL};
            }
        }

        if (!context.CanStart(1500))
            return {NetTaskDisposition::Retry, 60};
        NetHttpResponse response;
        const NetHttpResult transport = NetAuthSession_GetJson(
            "/api/v1/device/mailbox?limit=5",
            context,
            kPullResponseLimit,
            response);
        if (transport != NetHttpResult::Ok)
        {
            Serial.printf("[设备收件] 拉取失败，传输结果=%u。\n", static_cast<unsigned>(transport));
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }
        if (response.status_code == 401 || response.status_code == 403)
            return {NetTaskDisposition::AuthBlocked, 0};
        if (response.status_code != 200)
        {
            Serial.printf("[设备收件] 拉取失败，HTTP=%d。\n", response.status_code);
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        JsonDocument document;
        const DeserializationError error = deserializeJson(document, response.body);
        JsonArrayConst items = document["items"].as<JsonArrayConst>();
        if (error || items.isNull() || items.size() > kBatchCapacity)
        {
            Serial.printf("[设备收件] 服务端响应格式无效：%s。\n", error.c_str());
            return {NetTaskDisposition::PermanentFailure, 0};
        }

        const uint64_t server_acknowledged =
            document["acknowledged_sequence"] | static_cast<uint64_t>(0);
        /*
         * 空信箱也会返回 acknowledged_sequence。若它与本地连续游标相同，主循环没有任何
         * 状态需要落盘，不应仅为这个只读回显占用跨核交接区；否则紧接着启动的下一轮任务
         * 可能把空响应误判成“上批消息尚未落盘”。只有服务端确实领先时才请求主循环对齐。
         */
        if (server_acknowledged > accepted_sequence)
            SetServerCursorHandoff(server_acknowledged);

        uint8_t imported = 0;
        for (JsonObjectConst object : items)
        {
            SysMailboxIncomingMessage message;
            if (!ParseEnvelope(object, message) || !PushHandoff(message))
            {
                Serial.println("[设备收件] 消息字段或本地交接区无效，未确认本批数据。");
                return {NetTaskDisposition::PermanentFailure, 0};
            }
            ++imported;
        }
        if (imported > 0)
            Serial.printf("[设备收件] 已拉取 %u 封，等待主循环可靠落盘。\n", static_cast<unsigned>(imported));
        return {NetTaskDisposition::Complete, 0};
    }

    void MainUpdate()
    {
        const uint32_t now = millis();
        if (s_nextMainUpdateRetryMs != 0 && static_cast<int32_t>(now - s_nextMainUpdateRetryMs) < 0)
            return;

        uint64_t server_sequence = 0;
        if (PeekServerCursorHandoff(server_sequence))
        {
            if (!SysMailbox_ReconcileServerAcknowledged(server_sequence))
            {
                /* NVS 临时不可用时保留跨核游标，但每秒最多重试一次且只报告一次，避免主循环刷屏。 */
                if (!s_cursorFailureReported)
                {
                    Serial.println("[设备收件] 服务端确认游标暂未能写入本地，保留交接数据并稍后重试。");
                    s_cursorFailureReported = true;
                }
                s_nextMainUpdateRetryMs = now + 1000;
                return;
            }
            DropServerCursorHandoff();
            s_cursorFailureReported = false;
            s_nextMainUpdateRetryMs = 0;
        }

        SysMailboxIncomingMessage message;
        while (PeekHandoff(message))
        {
            const SysMailboxAcceptResult result = SysMailbox_Accept(message);
            if (result == SysMailboxAcceptResult::Accepted ||
                result == SysMailboxAcceptResult::AlreadyAccepted)
            {
                DropHandoff();
                s_messageFailureReported = false;
                s_lastMessageFailure = SysMailboxAcceptResult::Accepted;
                s_nextMainUpdateRetryMs = 0;
                continue;
            }

            /*
             * 落盘失败时交接数据始终保留。主循环每秒重试一次即可；同一种故障只报告一次，
             * 防止 NVS 暂不可用或信箱满时按每帧速度刷满串口并进一步拖慢主循环。
             */
            if (!s_messageFailureReported || result != s_lastMessageFailure)
            {
                if (result == SysMailboxAcceptResult::QueueFull)
                    Serial.println("[设备收件] 本地信箱已满，保留服务器消息并稍后重试。");
                else if (result == SysMailboxAcceptResult::SequenceGap)
                    Serial.println("[设备收件] 收件序号不连续，保留本批并等待前序消息。");
                else if (result == SysMailboxAcceptResult::InvalidMessage)
                    Serial.println("[设备收件] 消息字段未通过本地校验，保留现场并暂停落盘。");
                else
                    Serial.println("[设备收件] 本地信箱存储暂不可用，保留消息并稍后重试。");
                s_messageFailureReported = true;
                s_lastMessageFailure = result;
            }
            s_nextMainUpdateRetryMs = now + 1000;
            break;
        }
    }
}

bool NetMailboxReceive_Register()
{
    if (!s_handoff)
    {
        s_handoff = static_cast<HandoffBuffer *>(
            heap_caps_calloc(1, sizeof(HandoffBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_handoff)
        {
            Serial.println("[设备收件] 无法在 PSRAM 建立跨核交接区。");
            return false;
        }
    }

    NetTaskDefinition definition;
    definition.task_id = NET_TASK_MAILBOX_RECEIVE;
    definition.name = "设备信箱拉取";
    definition.execute = Execute;
    definition.main_update = MainUpdate;
    definition.trigger_mask = NET_TASK_TRIGGER_COMMON_CYCLE;
    definition.auth_requirement = NetTaskAuthRequirement::DeviceSession;
    return NetTaskRegistry_Register(definition);
}
