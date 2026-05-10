/*
【模块职责】BLE 接收队列接口。跨核心缓冲 WebBLE 写入字符串，避免 BLE 回调直接写文件、切页面和刷新 UI。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>

// Thread-safe bounded mailbox for BLE RX payloads.
// BLE callbacks run on Core 0; AppManager consumes messages on the UI/main loop.
// 【接口说明】由 BLE 写入回调调用，把原始命令字符串放入跨核心队列，队列满时移除最旧消息。
void SysBleQueue_Push(const String& msg);
bool SysBleQueue_Pop(String& out);
// 【接口说明】返回当前 BLE 队列长度，便于调试网页连发命令是否堆积。
size_t SysBleQueue_Size();
