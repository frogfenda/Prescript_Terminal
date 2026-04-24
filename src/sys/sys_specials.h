#pragma once
#include <Arduino.h>

// 统一的渲染数据包，UI层只管拿去画
struct DrawResult
{
    String title;    // 弹窗标题
    String text;     // 实际文字
    uint16_t color;  // 专属颜色
    bool is_special; // 是否为特殊人物拦截
};

class SysSpecials
{
private:
    DrawResult current_draw;

public:
    void begin();
    void rollRandom();                       // 核心：摇骰子抽卡！
    void setCustom(const char *custom_text); // 处理外界塞入的指令
    // 在类定义中增加：
    void forceDrawByID(const String &id);
    DrawResult getResult() { return current_draw; }
};

extern SysSpecials sysSpecials;