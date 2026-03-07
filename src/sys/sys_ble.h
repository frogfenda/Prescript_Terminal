// 文件：src/sys/sys_ble.h
#ifndef SYS_BLE_H
#define SYS_BLE_H

void SysBLE_Init();

// 【新增】：向手机网页主动发送数据的接口声明
void SysBLE_Notify(const char* data);

#endif