/*
【模块职责】拥有提取部身份数据和星级索引，向App提供复制式抽取接口。
【生命周期】启动时加载当前语言；语言切换会重建内部池。调用方只得到值副本，因此不会持有失效的内部指针。
*/
#pragma once

#include <Arduino.h>
#include "lang/terminal_lang.h"

struct IdentityData
{
    String sinner;
    String id_name;
    int star = 0;
    int walp = 0;
};

namespace SysIdentityCatalog
{
    /** 从FATFS加载指定语言并重建星级索引；失败时清空旧语言数据。 */
    bool Load(SystemLang_t lang);

    /** 查询指定星级是否至少有一条有效记录。 */
    bool HasStar(uint8_t star);

    /**
     * 从指定星级随机复制一条身份到out；内部池重载后out仍然有效。
     * 指定星级为空或参数非法时返回false。
     */
    bool DrawByStar(uint8_t star, IdentityData &out);

    /** 身份池总数；仅用于可用性判断和诊断。 */
    int Count();
}
