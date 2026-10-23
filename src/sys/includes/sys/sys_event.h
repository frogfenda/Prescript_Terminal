/*
【模块职责】同步事件总线接口。命令执行由 CommandRegistry 直接调用业务 Handler；事件总线只负责同步广播和 NFC 输入解耦。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_event.h
#pragma once
#include <Arduino.h>
#include <string.h>

// 事件总线只承载真正的一对多业务通知，不承载单个命令的执行入口。
enum SysEventID {
    EVT_BLE_SYNC_REQ,
    EVT_NFC_SCANNED,
};

// 同步请求是一个真正的一对多通知；各业务模块只按 scope 回传自己的数据。
struct Evt_BleSyncReq_t { const char* scope; };

// 【函数说明】判断一次蓝牙同步请求是否属于当前模块；空载荷和 ALL 都按旧版全量同步处理。
inline bool SysEvent_BleSyncScopeMatches(void* payload, const char* scope)
{
    Evt_BleSyncReq_t* req = (Evt_BleSyncReq_t*)payload;
    if (!req || !req->scope || strcmp(req->scope, "ALL") == 0)
        return true;
    return strcmp(req->scope, scope) == 0;
}
// 定义一个 NFC 电报包裹
struct Evt_NfcScanned_t {
    char uid[32];     // 卡片的物理 UID
    char payload[512]; // 解析出的文本指令 (如 "PRE:ZH:生成E.G.O侵蚀警告")
};
// 邮局接口声明
typedef void (*SysEventCallback)(void* payload);

// 【接口说明】订阅一个系统事件，事件发布时回调会被同步调用。
void SysEvent_Subscribe(SysEventID evt, SysEventCallback cb);
void SysEvent_Publish(SysEventID evt, void* payload);
