#pragma once
#include <Arduino.h>
#include "ui_theme.h"

// Tiny frame gate helper. It intentionally stays simple: Apps keep their own state,
// UIClock only decides whether enough time has passed for the next frame.
class UIFrameGate {
private:
    uint32_t last_ms = 0;
public:
    void reset(uint32_t now = millis()) { last_ms = now; }
    bool due(uint32_t interval_ms) {
        uint32_t now = millis();
        if (last_ms == 0 || now - last_ms >= interval_ms) {
            last_ms = now;
            return true;
        }
        return false;
    }
};

bool UIClock_Due(uint32_t &last_ms, uint32_t interval_ms);
