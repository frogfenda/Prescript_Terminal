/* 【模块职责】特殊库正文、触发器、链条和独立进度；展示快照归提醒出口。仅主循环调用。 */
#pragma once
#include "sys/resources/sys_prescript.h"
/** 预选令牌；选择不推进，接收方准备好快照后commit。chain为空时无需写进度。 */
struct PrescriptSelection {
    SystemLang_t lang = LANG_ZH;
    PrescriptId chain, expected, step;
    uint32_t revision = 0, rules_revision = 0;
};
class SysSpecials {
    SystemLang_t current_lang_ = LANG_ZH;
public:
    void begin(SystemLang_t lang);
    bool selectRandom(PrescriptContent &out, PrescriptSelection &selection) const;
    /** 保留旧SPC.FIRE从第一步开始的语义，同时接受UUID；未知ID生成检索失败快照。 */
    bool selectForced(const String &id, PrescriptContent &out, PrescriptSelection &selection) const;
    /** 验证预选未过期，持久化成功才消费进度；失败不修改状态。 */
    bool commit(const PrescriptSelection &selection);
    bool hasID(const String &id) const;
    bool metaAt(uint16_t index, JsonDocument &out) const;
    bool textByID(const String &id, String &out) const;
    /** 整库编辑：同库UUID、版本、本库引用、概率和进度校验通过后同文件提交内容/规则。 */
    SysOpResult replaceLibrary(const JsonDocument &document, uint32_t expected_revision);

};
extern SysSpecials sysSpecials;
