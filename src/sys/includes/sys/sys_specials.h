/*
【模块职责】特殊指令接口。管理人物链条、异想体/纯特殊指令、概率抽取、颜色和 WebBLE 同步。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>

// 统一的渲染数据包，UI层只管拿去画
struct DrawResult
{
    String title;    // 弹窗标题
    String text;     // 实际文字
    uint16_t color;  // 专属颜色
    bool is_special; // 是否为特殊人物拦截
    String audio_bind; // 【新增】：绑定的音频标识符
};

class SysSpecials
{
private:
    DrawResult current_draw;

public:
    // 【接口说明】加载当前语言特殊指令 JSON 并准备抽取池。
    void begin();
    void rollRandom();                       // 核心：摇骰子抽卡！
    void setCustom(const char *custom_text); // 处理外界塞入的指令
    // 在类定义中增加：
    // 【接口说明】按 ID 强制选中特殊指令。
    void forceDrawByID(const String &id);
    DrawResult getResult() { return current_draw; }
    // 【新增】：向蓝牙同步所有特异点元数据 (不包含庞大文本)
    void syncMetaData();
    // 【新增】：按需索取具体的文本指令
    // 【接口说明】向网页同步指定特殊指令正文。
    void syncTextByID(const String &id);
};

extern SysSpecials sysSpecials;
