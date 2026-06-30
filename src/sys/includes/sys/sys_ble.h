/*
【模块职责】BLE 通信接口。初始化 WebBLE 服务，并提供 Notify 让固件主动向网页发送 SYNC、ACK、LANG、SPC 等文本消息。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_ble.h
#ifndef SYS_BLE_H
#define SYS_BLE_H

void SysBLE_Init();

// 【新增】：向手机网页主动发送数据的接口声明
void SysBLE_Notify(const char* data);

#endif
