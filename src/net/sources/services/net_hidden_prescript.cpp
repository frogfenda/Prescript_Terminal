// 文件：src/net/sources/services/net_hidden_prescript.cpp
/*
【模块职责】在 Core 0 拉取隐秘指令，并在主循环复用 SysRouter 的日程写入链路。
该模块只是 task_id=1 的普通任务；NetService 不了解其 URL、JSON 或结果落地细节。
*/
#include "net/services/net_hidden_prescript.h"

#include <ArduinoJson.h>
#include <mutex>
#include <queue>
#include "net/net_http_transport.h"
#include "net/net_task_registry.h"
#include "sys/sys_constants.h"
#include "sys/sys_router.h"

namespace
{
    struct HiddenPrescriptItem
    {
        uint32_t target_time = 0;
        String title;
        String text;
    };

    constexpr size_t kMailboxCapacity = 5;
    std::queue<HiddenPrescriptItem> s_mailbox;
    std::mutex s_mailboxMutex;

    void PushItem(uint32_t target_time, const String &title, const String &text)
    {
        std::lock_guard<std::mutex> lock(s_mailboxMutex);
        while (s_mailbox.size() >= kMailboxCapacity)
            s_mailbox.pop();
        HiddenPrescriptItem item;
        item.target_time = target_time;
        item.title = title;
        item.text = text;
        s_mailbox.push(item);
    }

    bool PopItem(HiddenPrescriptItem &out)
    {
        std::lock_guard<std::mutex> lock(s_mailboxMutex);
        if (s_mailbox.empty())
            return false;
        out = s_mailbox.front();
        s_mailbox.pop();
        return true;
    }

    NetTaskExecutionResult Execute(const NetTaskInvocation &, const NetTaskContext &context)
    {
        Serial.println("[网络任务/隐秘指令] 开始请求服务端。 ");

        const uint32_t remaining_ms = context.RemainingMs();
        if (remaining_ms <= 1200)
        {
            Serial.println("[网络任务/隐秘指令] 会话剩余时间不足，本轮暂不发起请求。 ");
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        NetHttpResponse response;
        const NetHttpResult transport = NetHttp_GetJsonUrl(
            PrescriptConst::NETWORK_SYNC_URL,
            context,
            4096,
            response);
        if (transport != NetHttpResult::Ok)
        {
            Serial.printf("[网络任务/隐秘指令] HTTPS 传输失败，结果=%u。\n",
                          static_cast<unsigned>(transport));
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }
        if (response.status_code != 200)
        {
            Serial.printf("[网络任务/隐秘指令] 请求失败，HTTP=%d。\n", response.status_code);
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        JsonDocument document;
        const DeserializationError error = deserializeJson(document, response.body);
        if (error || !document.is<JsonArray>())
        {
            Serial.printf("[网络任务/隐秘指令] JSON 解析失败：%s。\n", error.c_str());
            return {NetTaskDisposition::Retry, 5UL * 60UL};
        }

        uint8_t imported_count = 0;
        for (JsonObject object : document.as<JsonArray>())
        {
            if (imported_count >= kMailboxCapacity)
                break;

            const uint32_t target_time = object["time"] | 0;
            if (target_time == 0)
                continue;

            const String title = object["title"] | "隐秘行动";
            const String text = object["ps"] | "";
            PushItem(target_time, title, text);
            ++imported_count;
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        Serial.printf("[网络任务/隐秘指令] 已接收 %u 条有效日程。\n",
                      static_cast<unsigned>(imported_count));
        return {NetTaskDisposition::Complete, 0};
    }

    void MainUpdate()
    {
        HiddenPrescriptItem item;
        while (PopItem(item))
            SysRouter_ProcessAPI(item.target_time, item.title, item.text);
    }
}

bool NetHiddenPrescript_Register()
{
    NetTaskDefinition definition;
    definition.task_id = NET_TASK_HIDDEN_PRESCRIPT_PULL;
    definition.name = "隐秘指令拉取";
    definition.execute = Execute;
    definition.main_update = MainUpdate;
    /* 旧域名当前不可用；保留显式任务入口，待迁移到新服务端的认证接口后再恢复自动触发。 */
    definition.trigger_mask = NET_TASK_TRIGGER_NONE;
    return NetTaskRegistry_Register(definition);
}
