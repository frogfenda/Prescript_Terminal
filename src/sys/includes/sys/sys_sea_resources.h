/*
【模块职责】持有Sea应用当前语言的叙事目录，使JSON在系统启动或语言切换时完成解析。
【能力边界】这里只协调资源缓存，不绘制海面、不推进句子、不播放音频。
*/
#pragma once

#include "lang/terminal_lang.h"

class SysNarrativeCatalog;

namespace SysSeaResources
{
    /** 预加载指定语言的Sea叙事；失败时目录为空，Sea仍可运行纯流体效果。 */
    bool Load(SystemLang_t lang);

    /** 返回系统持有的只读叙事目录；语言切换后的旧场景指针失效。 */
    const SysNarrativeCatalog &Narrative();
}
