/* BLE 同步共用序列化出口；字段由调用者填入 JsonDocument，统一处理引号/反斜杠/控制字符。 */
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
/** 主循环调用；prefix 如 SYNC:ALM:。只在完整序列化成功后发送，不表示对端已经接收。 */
bool SysBLE_NotifyJson(const char *prefix,const JsonDocument &document);
