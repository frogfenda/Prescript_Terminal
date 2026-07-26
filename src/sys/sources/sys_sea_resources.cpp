/*
【模块职责】把Sea当前语言叙事接入启动预热链路，避免首次打开应用时再读取和解析JSON。
*/
#include "sys/sys_sea_resources.h"

#include "sys/sys_narrative.h"
#include "lang/terminal_lang.h"

namespace
{
    SysNarrativeCatalog g_narrative;
}

bool SysSeaResources::Load(SystemLang_t lang)
{
    return g_narrative.load(TerminalLang::SeaNarrativePath(lang));
}

const SysNarrativeCatalog &SysSeaResources::Narrative()
{
    return g_narrative;
}
