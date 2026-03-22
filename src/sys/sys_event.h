// 文件：src/sys/sys_event.h
#pragma once
#include <Arduino.h>

// 1. 在 enum SysEventID 里面加一行：
enum SysEventID {
    EVT_SCHEDULE_ADD,
    EVT_SCHEDULE_DEL,
    EVT_ALARM_ADD,
    EVT_ALARM_DEL,
    EVT_POMODORO_UPDATE,
    EVT_NOTIFY_CUSTOM,
    EVT_BLE_SYNC_REQ,
    EVT_NFC_SCANNED,
    EVT_PRESCRIPT_ADD,   // 【新增】：添加都市指令频道
    EVT_WIFI_SET,
    EVT_PRESCRIPT_DEL   // 【新增】：删除都市指令频道
    
};


// 2. 在下方结构体区域，增加弹窗专用电报：
struct Evt_Notify_t { const char* text; bool keep_stack; };
// 2. 定义电报数据包 (Payload)
struct Evt_SchAdd_t { uint32_t tt; const char* title; const char* text; bool is_hidden; };
struct Evt_SchDel_t { const char* title; };
struct Evt_AlmAdd_t { const char* name; int hour; int min; const char* text; };
struct Evt_AlmDel_t { const char* name; };
struct Evt_PomUpd_t { int idx; const char* name; int w; int r; };
// 2. 在下方结构体区域，增加对应的数据包：
struct Evt_PreAdd_t { 
    int lang; // 0代表中文(ZH)，1代表英文(EN)
    const char* text; 
};
struct Evt_PreDel_t { 
    int lang; 
    const char* text; 
};
// 定义一个 NFC 电报包裹
struct Evt_NfcScanned_t {
    char uid[32];     // 卡片的物理 UID
    char payload[512]; // 解析出的文本指令 (如 "PRE:ZH:生成E.G.O侵蚀警告")
};
struct Evt_WifiSet_t { 
    const char* ssid; 
    const char* pass; 
};
// 3. 邮局接口声明
typedef void (*SysEventCallback)(void* payload);

void SysEvent_Subscribe(SysEventID evt, SysEventCallback cb);
void SysEvent_Publish(SysEventID evt, void* payload);