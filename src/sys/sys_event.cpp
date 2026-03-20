// 文件：src/sys/sys_event.cpp
#include "sys_event.h"

#define MAX_SUBSCRIBERS 20

// 订阅者花名册
struct Subscriber {
    SysEventID evt;
    SysEventCallback cb;
};

static Subscriber g_subscribers[MAX_SUBSCRIBERS];
static int g_sub_count = 0;

// 登记订阅
void SysEvent_Subscribe(SysEventID evt, SysEventCallback cb) {
    if (g_sub_count < MAX_SUBSCRIBERS) {
        g_subscribers[g_sub_count].evt = evt;
        g_subscribers[g_sub_count].cb = cb;
        g_sub_count++;
    }
}

// 广播分发
void SysEvent_Publish(SysEventID evt, void* payload) {
    for (int i = 0; i < g_sub_count; i++) {
        if (g_subscribers[i].evt == evt && g_subscribers[i].cb != nullptr) {
            g_subscribers[i].cb(payload); // 精准投递！
        }
    }
}