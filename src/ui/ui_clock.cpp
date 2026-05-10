/*
【模块职责】轻量帧率门控实现。到达间隔时更新 last_ms 并返回 true，供音量界面等页面降低无效刷屏。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "ui_clock.h"

// 【函数说明】比较当前 millis 与 last_ms；达到 interval_ms 时更新 last_ms 并允许调用者刷新一帧。
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
