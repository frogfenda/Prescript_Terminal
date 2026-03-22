// 文件：src/sys/sys_network.h
#pragma once
#include <Arduino.h>

enum NetworkState {
    NET_DISCONNECTED,
    NET_CONNECTING,
    NET_SYNCING_NTP,    // 正在解析DNS并对时
    NET_FETCHING_API,   // 正在后台拉取指令
    NET_SYNC_SUCCESS,   // 完美完成
    NET_CONNECT_FAILED, // 连不上WiFi
    NET_SYNC_FAILED     // 对时超时
};

void Network_Init();
// 【核心接口】：打个响指，在后台一气呵成完成（连网->NTP->API->断网保存）
void Network_StartSync(); 
NetworkState Network_GetState();