/*
【模块职责】纺织机回复池接口。

本模块通过统一资源IO解析当前语言的oracle.json短答案，并按type + weight抽取。
它不接入 sysSpecials，也不写入当前特殊指令结果，避免和特殊指令音频/剧情状态互相污染。
*/
#pragma once
#include <Arduino.h>
#include "lang/terminal_lang.h"

struct OracleAnswer
{
    String id;
    String type;
    String text;
};

class SysOracle
{
public:
    /** 按当前系统语言预加载答案池；资源缺失时保留内置兜底答案。 */
    void begin();

    /** 按 type 抽取一条答案。type 例如 "weaver" / "food"。 */
    bool drawByType(const char* type, SystemLang_t lang, OracleAnswer& out);

private:
    bool ensureLoaded(SystemLang_t lang);
};

extern SysOracle sysOracle;
