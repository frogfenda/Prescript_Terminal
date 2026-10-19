#include "sys/sys_ble_json.h"
#include "sys/sys_ble.h"
bool SysBLE_NotifyJson(const char *prefix,const JsonDocument &document)
{
    if (!prefix || document.overflowed()) return false;
    const size_t bytes=measureJson(document);
    String output;
    if (!output.reserve(strlen(prefix)+bytes+1)) return false;
    output=prefix;
    if (serializeJson(document,output)!=bytes) return false;
    SysBLE_Notify(output.c_str()); return true;
}
