#ifndef __SYS_NETWORK_H
#define __SYS_NETWORK_H
#include <Arduino.h>

// 极其干净的网络状态机枚举
enum NetworkState {
    NET_IDLE,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_SYNCING_NTP,
    NET_SYNC_SUCCESS,
    NET_FAIL
};

void Network_Connect(const char* ssid, const char* pass);
void Network_StartNTP();
void Network_Disconnect();
NetworkState Network_GetState();
void Network_Update(); 

#endif