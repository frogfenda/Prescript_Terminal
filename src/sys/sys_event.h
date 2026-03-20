// 文件：src/sys/sys_event.h
#pragma once
#include <Arduino.h>

// 1. 定义事件频道 ID
enum SysEventID {
    EVT_SCHEDULE_ADD,
    EVT_SCHEDULE_DEL,
    EVT_ALARM_ADD,
    EVT_ALARM_DEL,
    EVT_POMODORO_UPDATE
};

// 2. 定义电报数据包 (Payload)
struct Evt_SchAdd_t { uint32_t tt; const char* title; const char* text; bool is_hidden; };
struct Evt_SchDel_t { const char* title; };
struct Evt_AlmAdd_t { const char* name; int hour; int min; const char* text; };
struct Evt_AlmDel_t { const char* name; };
struct Evt_PomUpd_t { int idx; const char* name; int w; int r; };

// 3. 邮局接口声明
typedef void (*SysEventCallback)(void* payload);

void SysEvent_Subscribe(SysEventID evt, SysEventCallback cb);
void SysEvent_Publish(SysEventID evt, void* payload);