/*
【模块职责】轻量帧率门控接口。用 last_ms 与 interval_ms 控制页面按固定帧率刷新。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
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

// 【接口说明】用 last_ms 与 interval_ms 判断是否到达下一帧；到点时更新 last_ms 并返回 true。
bool UIClock_Due(uint32_t &last_ms, uint32_t interval_ms);
