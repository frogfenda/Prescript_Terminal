/* 【模块职责】特殊规则、链条及独立进度；正文和展示结果归内容库。仅主循环调用。 */
#pragma once
#include "sys/resources/sys_prescript_catalog.h"
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
    bool rollRandom();
    bool hasID(const String &id) const;
    bool metaAt(uint16_t index, JsonDocument &out) const;
    bool textByID(const String &id, String &out) const;
    /** 整体编辑基础：版本、引用、概率预算和进度一致性校验通过，保存成功才发布。暂不新增协议。 */
    SysOpResult replaceRules(const JsonDocument &document, uint32_t expected_revision);
    /** 删除前校验该语言磁盘规则；不切换已加载规则、不推进状态。防止USB编辑后旧引用保护过期。 */
    static bool references(SystemLang_t lang, SysPsramVector<PrescriptId> &out);
};
extern SysSpecials sysSpecials;
