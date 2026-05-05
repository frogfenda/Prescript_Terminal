#pragma once
#include <Arduino.h>

// Thread-safe bounded mailbox for BLE RX payloads.
// BLE callbacks run on Core 0; AppManager consumes messages on the UI/main loop.
void SysBleQueue_Push(const String& msg);
bool SysBleQueue_Pop(String& out);
size_t SysBleQueue_Size();
