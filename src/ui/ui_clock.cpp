#include "ui_clock.h"

bool UIClock_Due(uint32_t &last_ms, uint32_t interval_ms)
{
    uint32_t now = millis();
    if (last_ms == 0 || now - last_ms >= interval_ms)
    {
        last_ms = now;
        return true;
    }
    return false;
}
