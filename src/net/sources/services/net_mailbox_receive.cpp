/*
【模块职责】广播与私人收件共用 HTTP/JSON 实现，但独立注册、独立游标、独立跨核交接区。
【执行顺序】内建注册表先注册广播再注册私人任务；每路每轮最多五封，不循环耗尽广播积压。
【线程边界】Core 0 校验完整响应后一次发布整批；main_update 在主循环可靠落盘，下一轮才 ACK。
【生命周期】广播的 expires_at 是服务端领取期限，不擅自取消已落盘的未来定时消息。
*/
#include "net/services/net_mailbox_receive.h"

#include <ArduinoJson.h>
#include <cctype>
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

    uint16_t ParseRgb888To565(const char *value, bool &valid)
    {
        valid = false;
        if (!value || strlen(value) != 7 || value[0] != '#')
            return 0x07FF;
        for (size_t index = 1; index < 7; ++index)
            if (!isxdigit(static_cast<unsigned char>(value[index])))
                return 0x07FF;
        const unsigned long rgb = strtoul(value + 1, nullptr, 16);
        valid = true;
        return static_cast<uint16_t>(((rgb >> 8) & 0xF800) |
                                     ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
    }

    /**
     * 同一实现的两个常驻实例。Mutex 只保护短暂内存复制，不能在锁内做 HTTP 或 NVS 写入。
     * 重试和故障日志标志仅在主循环使用；网络核只访问加锁后的交接区及已落盘游标快照。
     */
    class MailboxReceiver
    {
    public:
        explicit MailboxReceiver(SysMailboxKind mailbox) : mailbox_(mailbox) {}

        bool Prepare()
        {
            if (!handoff_)
                handoff_ = static_cast<HandoffBuffer *>(heap_caps_calloc(
                    1, sizeof(HandoffBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!parse_scratch_)
                parse_scratch_ = static_cast<SysMailboxIncomingMessage *>(heap_caps_calloc(
                    1, sizeof(SysMailboxIncomingMessage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!handoff_ || !parse_scratch_)
                Serial.printf("[设备收件] %s无法在 PSRAM 建立交接区。\n", Label());
            return handoff_ != nullptr && parse_scratch_ != nullptr;
        }

        NetTaskExecutionResult Execute(const NetTaskContext &context)
        {
            if (!HandoffIsEmpty())
                return {NetTaskDisposition::Retry, 60};

            const uint64_t accepted = SysMailbox_GetAcceptedSequence(mailbox_);
            if (accepted > 0)
            {
                JsonDocument body_document;
                body_document["mailbox"] = ApiKind();
                body_document["through_sequence"] = accepted;
                String body;
                serializeJson(body_document, body);
                NetHttpResponse response;
                const NetHttpResult transport = NetAuthSession_PostJson(
                    "/api/v1/device/mailbox/ack", body, context, kAckResponseLimit, response);
                if (transport == NetHttpResult::Ok &&
                    (response.status_code == 401 || response.status_code == 403))
                    return {NetTaskDisposition::AuthBlocked, 0};
                if (transport != NetHttpResult::Ok || response.status_code != 200)
                {
                    Serial.printf("[设备收件] %s游标确认失败：传输=%u，HTTP=%d。\n",
                                  Label(), static_cast<unsigned>(transport), response.status_code);
                    return {NetTaskDisposition::Retry, 300};
                }
                JsonDocument result;
                const DeserializationError error = deserializeJson(result, response.body);
                if (error || strcmp(result["mailbox"] | "", ApiKind()) != 0 ||
                    !result["acknowledged_sequence"].is<uint64_t>() ||
                    result["acknowledged_sequence"].as<uint64_t>() < accepted)
                {
                    Serial.printf("[设备收件] %s确认响应无效，保留本地状态。\n", Label());
                    return {NetTaskDisposition::PermanentFailure, 0};
                }
            }

            if (!context.CanStart(1500))
                return {NetTaskDisposition::Retry, 60};
            const char *path = mailbox_ == SysMailboxKind::Broadcast
                ? "/api/v1/device/mailbox?limit=5&mailbox=broadcast"
                : "/api/v1/device/mailbox?limit=5&mailbox=private";
            NetHttpResponse response;
            const NetHttpResult transport = NetAuthSession_GetJson(path, context, kPullResponseLimit, response);
            if (transport != NetHttpResult::Ok)
            {
                Serial.printf("[设备收件] %s拉取失败，传输结果=%u。\n", Label(), static_cast<unsigned>(transport));
                return {NetTaskDisposition::Retry, 300};
            }
            if (response.status_code == 401 || response.status_code == 403)
                return {NetTaskDisposition::AuthBlocked, 0};
            if (response.status_code != 200)
            {
                Serial.printf("[设备收件] %s拉取失败，HTTP=%d。\n", Label(), response.status_code);
                return {NetTaskDisposition::Retry, 300};
            }

            JsonDocument document;
            const DeserializationError error = deserializeJson(document, response.body);
            const JsonArrayConst items = document["items"].as<JsonArrayConst>();
            const char *cursor_key = mailbox_ == SysMailboxKind::Broadcast
                ? "broadcast_acknowledged_sequence" : "private_acknowledged_sequence";
            if (error || items.isNull() || items.size() > kBatchCapacity ||
                !document[cursor_key].is<uint64_t>() ||
                (mailbox_ == SysMailboxKind::Broadcast && !document["broadcast_subscribed"].is<bool>()))
            {
                Serial.printf("[设备收件] %s响应格式无效：%s。\n", Label(), error.c_str());
                return {NetTaskDisposition::PermanentFailure, 0};
            }
            if (mailbox_ == SysMailboxKind::Broadcast &&
                !document["broadcast_subscribed"].as<bool>() && items.size() != 0)
            {
                Serial.println("[设备收件] 未订阅广播却收到广播内容，未移交本批。");
                return {NetTaskDisposition::PermanentFailure, 0};
            }

            const uint64_t server_acknowledged = document[cursor_key].as<uint64_t>();
            uint64_t previous = server_acknowledged;
            for (JsonVariantConst item : items)
            {
                if (!item.is<JsonObjectConst>() || !parse_scratch_ ||
                    !ParseEnvelope(item.as<JsonObjectConst>(), *parse_scratch_) ||
                    parse_scratch_->sequence <= previous ||
                    (mailbox_ == SysMailboxKind::Private && parse_scratch_->sequence - previous != 1))
                {
                    Serial.printf("[设备收件] %s字段或序号无效，未移交本批。\n", Label());
                    return {NetTaskDisposition::PermanentFailure, 0};
                }
                // 广播会过滤撤回/过期记录，仅验证递增；不伪造缺失序号的消息。
                previous = parse_scratch_->sequence;
            }

            /*
             * 完整校验之后持锁一次发布，不让主循环看到半个批次。空响应且游标未领先时没有
             * 待落盘状态，不能占住交接区；未订阅广播也不会影响另一个私人接收实例。
             */
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!handoff_ || handoff_->count != 0 || handoff_->server_cursor_pending)
                    return {NetTaskDisposition::Retry, 60};
                handoff_->head = 0;
                for (JsonObjectConst object : items)
                    ParseEnvelope(object, handoff_->items[handoff_->count++]);
                handoff_->server_acknowledged_sequence = server_acknowledged;
                handoff_->server_cursor_pending = server_acknowledged > accepted;
            }
            if (items.size() > 0)
                Serial.printf("[设备收件] %s已拉取 %u 封，等待可靠落盘。\n", Label(), static_cast<unsigned>(items.size()));
            return {NetTaskDisposition::Complete, 0};
        }

        void MainUpdate()
        {
            const uint32_t now = millis();
            if (next_retry_ms_ != 0 && static_cast<int32_t>(now - next_retry_ms_) < 0)
                return;
            uint64_t server_sequence = 0;
            bool cursor_pending = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!handoff_)
                    return;
                cursor_pending = handoff_->server_cursor_pending;
                server_sequence = handoff_->server_acknowledged_sequence;
            }
            if (cursor_pending)
            {
                if (!SysMailbox_ReconcileServerAcknowledged(mailbox_, server_sequence))
                {
                    if (!cursor_failure_reported_)
                        Serial.printf("[设备收件] %s游标暂未落盘，保留交接数据。\n", Label());
                    cursor_failure_reported_ = true;
                    next_retry_ms_ = now + 1000;
                    return;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                handoff_->server_cursor_pending = false;
                cursor_failure_reported_ = false;
            }

            SysMailboxIncomingMessage message;
            while (Peek(message))
            {
                const SysMailboxAcceptResult result = SysMailbox_Accept(message);
                if (result == SysMailboxAcceptResult::Accepted || result == SysMailboxAcceptResult::AlreadyAccepted)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    handoff_->head = static_cast<uint8_t>((handoff_->head + 1) % kBatchCapacity);
                    --handoff_->count;
                    message_failure_reported_ = false;
                    next_retry_ms_ = 0;
                    continue;
                }
                if (!message_failure_reported_ || result != last_failure_)
                {
                    const char *reason = result == SysMailboxAcceptResult::QueueFull ? "本地渠道容量已满"
                        : result == SysMailboxAcceptResult::SequenceGap ? "私人收件序号不连续"
                        : result == SysMailboxAcceptResult::InvalidMessage ? "消息字段无效"
                        : "本地存储暂不可用";
                    Serial.printf("[设备收件] %s%s，保留本批并稍后重试。\n", Label(), reason);
                    message_failure_reported_ = true;
                    last_failure_ = result;
                }
                next_retry_ms_ = now + 1000;
                return;
            }
        }

    private:
        const char *ApiKind() const { return mailbox_ == SysMailboxKind::Broadcast ? "broadcast" : "private"; }
        const char *Label() const { return mailbox_ == SysMailboxKind::Broadcast ? "广播信箱" : "私人信箱"; }

        bool HandoffIsEmpty()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return handoff_ && handoff_->count == 0 && !handoff_->server_cursor_pending;
        }

        bool Peek(SysMailboxIncomingMessage &message)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!handoff_ || handoff_->count == 0)
                return false;
            message = handoff_->items[handoff_->head];
            return true;
        }

        bool ParseEnvelope(JsonObjectConst object, SysMailboxIncomingMessage &message)
        {
            message = SysMailboxIncomingMessage{};
            const char *message_id = object["id"] | "";
            const char *sender = object["sender_account_id"] | "";
            const bool system_sender = object["sender_account_id"].isNull();
            const char *content = object["content"] | "";
            const char *type = object["type"] | "";
            bool color_valid = false;
            const uint16_t color = ParseRgb888To565(object["font_color"] | "", color_valid);
            if (strcmp(object["mailbox"] | "", ApiKind()) != 0 ||
                !object["sequence"].is<uint64_t>() || object["sequence"].as<uint64_t>() == 0 ||
                strlen(message_id) != 36 || (!system_sender && strlen(sender) != 36) ||
                strlen(content) == 0 || strlen(content) > SYS_MAILBOX_CONTENT_MAX || !color_valid)
                return false;
            if (strcmp(type, "instant_text") == 0)
                message.type = SysMailboxMessageType::InstantText;
            else if (strcmp(type, "scheduled_text") == 0 && object["trigger_epoch"].is<int64_t>())
            {
                message.type = SysMailboxMessageType::ScheduledText;
                message.trigger_epoch = object["trigger_epoch"].as<int64_t>();
                if (message.trigger_epoch <= 0)
                    return false;
            }
            else
                return false;
            message.mailbox = mailbox_;
            message.sequence = object["sequence"].as<uint64_t>();
            message.font_color = color;
            strlcpy(message.message_id, message_id, sizeof(message.message_id));
            // 现有公开发送者字段保存账号 UUID；无发送账号的管理员消息统一标作 SYSTEM。
            strlcpy(message.sender_public_id, system_sender ? "SYSTEM" : sender, sizeof(message.sender_public_id));
            strlcpy(message.content, content, sizeof(message.content));
            return true;
        }

        const SysMailboxKind mailbox_;
        HandoffBuffer *handoff_ = nullptr;
        // 仅由 Core 0 校验响应时复用；主循环只读取已发布的 handoff_，不接触此临时区。
        // 避免约半 KiB 正文对象驻留在整个 Execute 调用的任务栈，叠加 HTTPS 调用链。
        SysMailboxIncomingMessage *parse_scratch_ = nullptr;
        std::mutex mutex_;
        uint32_t next_retry_ms_ = 0;
        bool cursor_failure_reported_ = false;
        bool message_failure_reported_ = false;
        SysMailboxAcceptResult last_failure_ = SysMailboxAcceptResult::Accepted;
    };

    MailboxReceiver s_broadcast(SysMailboxKind::Broadcast);
    MailboxReceiver s_private(SysMailboxKind::Private);

    NetTaskExecutionResult ExecuteBroadcast(const NetTaskInvocation &, const NetTaskContext &context)
    {
        return s_broadcast.Execute(context);
    }
    NetTaskExecutionResult ExecutePrivate(const NetTaskInvocation &, const NetTaskContext &context)
    {
        return s_private.Execute(context);
    }
    void UpdateBroadcast() { s_broadcast.MainUpdate(); }
    void UpdatePrivate() { s_private.MainUpdate(); }

    bool RegisterReceiver(MailboxReceiver &receiver, uint16_t id, const char *name,
                          NetTaskExecute execute, NetTaskMainUpdate update)
    {
        if (!receiver.Prepare())
            return false;
        NetTaskDefinition definition;
        definition.task_id = id;
        definition.name = name;
        definition.execute = execute;
        definition.main_update = update;
        definition.trigger_mask = NET_TASK_TRIGGER_COMMON_CYCLE;
        definition.auth_requirement = NetTaskAuthRequirement::DeviceSession;
        return NetTaskRegistry_Register(definition);
    }
}

bool NetBroadcastReceive_Register()
{
    return RegisterReceiver(s_broadcast, NET_TASK_BROADCAST_RECEIVE, "广播信箱拉取", ExecuteBroadcast, UpdateBroadcast);
}

bool NetMailboxReceive_Register()
{
    return RegisterReceiver(s_private, NET_TASK_MAILBOX_RECEIVE, "私人信箱拉取", ExecutePrivate, UpdatePrivate);
}
