/*
【模块职责】同步事件总线实现。每种事件最多保存固定数量回调，Publish 立即依次调用订阅者，因此事件回调应保持短小。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_event.cpp
#include "sys_event.h"
#include "sys_constants.h"

// 订阅者花名册
struct Subscriber {
    SysEventID evt;
    SysEventCallback cb;
};

static Subscriber g_subscribers[PrescriptConst::MAX_EVENT_SUBSCRIBERS];
static int g_sub_count = 0;

// 登记订阅
// 【函数说明】把回调加入指定事件的订阅表，超过固定容量时忽略新订阅。
void SysEvent_Subscribe(SysEventID evt, SysEventCallback cb) {
    if (g_sub_count < PrescriptConst::MAX_EVENT_SUBSCRIBERS) {
        g_subscribers[g_sub_count].evt = evt;
        g_subscribers[g_sub_count].cb = cb;
        g_sub_count++;
    }
}

// 广播分发
// 【函数说明】同步调用指定事件的所有回调；payload 只在调用期间有效，订阅者需要立即读取。
void SysEvent_Publish(SysEventID evt, void* payload) {
    for (int i = 0; i < g_sub_count; i++) {
        if (g_subscribers[i].evt == evt && g_subscribers[i].cb != nullptr) {
            g_subscribers[i].cb(payload); // 精准投递！
        }
    }
}
