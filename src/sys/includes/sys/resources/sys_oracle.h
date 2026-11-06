/* 【模块职责】纺织机分类与相对权重规则；正文统一通过内容UUID引用，去重状态只归本模块。 */
#pragma once
#include "sys/resources/sys_prescript_catalog.h"
struct OracleAnswer {
    PrescriptContent content;
};
class SysOracle {
    SystemLang_t loaded_lang_ = LANG_ZH;
    bool ensureLoaded(SystemLang_t lang);
public:
    void begin(SystemLang_t lang);
    /** 返回独立快照，当前内容库编辑/语言切换不影响已抽到的答案。缺失资源使用内置提示。 */
    bool drawByType(const char *type,SystemLang_t lang,OracleAnswer &out);
    /** 参数整体编辑基础：版本、分类权重溢出和全部内容引用校验后提交，暂不新增线协议。 */
    SysOpResult replaceRules(const JsonDocument &document,uint32_t expected_revision);
    /** 删除前只读校验磁盘引用，覆盖当前未激活的语言；规则格式或引用无效时拒绝删除内容。 */
    static bool references(SystemLang_t lang, SysPsramVector<PrescriptId> &out);
};
extern SysOracle sysOracle;
