/*
【模块职责】特殊指令接口。管理人物链条、异想体/纯特殊指令、概率抽取和颜色；蓝牙 SPC 组通过只读接口取数。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "lang/terminal_lang.h"

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
    SystemLang_t current_lang_ = LANG_ZH;

public:
    // 【接口说明】加载当前语言特殊指令 JSON 并准备抽取池。
    void begin(SystemLang_t lang);
    void rollRandom();                       // 核心：摇骰子抽卡！
    /** 处理外界塞入的指令；未指定颜色时保持既有青色。 */
    void setCustom(const char *custom_text, uint16_t font_color = 0x07FF);
    // 在类定义中增加：
    // 【接口说明】按 ID 强制选中特殊指令。
    void forceDrawByID(const String &id);
    DrawResult getResult() { return current_draw; }
    // 【接口说明】ID 是否属于已加载的人物链条或纯特殊指令。
    bool hasID(const String &id) const;
    // 【接口说明】按序号取一条元数据（先纯特殊指令、后人物链条），供 SPC.LIST 逐条输出；越界返回 false。
    bool metaAt(uint16_t index, JsonDocument &out) const;
    // 【接口说明】按 ID 取正文：人物链条取当前进度的下一条；找不到返回 false。
    bool textByID(const String &id, String &out) const;
};

extern SysSpecials sysSpecials;
