// 文件：src/sys/sys_network.h
#ifndef SYS_NETWORK_H
#define SYS_NETWORK_H

#include <Arduino.h>

enum NetworkState {
    NET_DISCONNECTED,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_SYNCING_NTP,
    NET_SYNC_SUCCESS,
    NET_SYNC_FAILED,
    NET_CONNECT_FAILED
};

void Network_Init();
void Network_Update();
void Network_Connect();
void Network_Disconnect();
NetworkState Network_GetState();

// 【新增】：高级网络调度接口
void Network_ForceNTP();        // 强制刷新时间（不改变WiFi连接）
void Network_AutoSyncTask();    // 触发后台开机静默对时任务

#endif