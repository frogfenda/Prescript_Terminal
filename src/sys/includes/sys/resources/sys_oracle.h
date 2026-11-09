/* 【模块职责】纺织机独立内容库、分类和相对权重；去重状态只归本模块。 */
#pragma once
#include "sys/resources/sys_prescript.h"
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
    /** 整库编辑：同库UUID、版本、权重和本库内容引用校验通过后同文件提交。 */
    SysOpResult replaceLibrary(const JsonDocument &document,uint32_t expected_revision);

};
extern SysOracle sysOracle;
