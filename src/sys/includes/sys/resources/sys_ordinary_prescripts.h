/* 【模块职责】FAT普通指令库，库内每条内容都是普通候选；与其它库没有跨库引用。 */
#pragma once
#include "sys/resources/sys_prescript.h"
namespace SysOrdinaryPrescripts {
    /** FAT挂载后加载双语；目标缺失时优先迁移旧集中JSON，再迁移旧TXT，原件保留。 */
    void Load();
    bool Ready(SystemLang_t lang);
    const PrescriptContent *Find(SystemLang_t lang, const PrescriptId &id);
    size_t Count(SystemLang_t lang);
    const PrescriptContent *At(SystemLang_t lang, size_t index);
    bool Draw(SystemLang_t lang, PrescriptContent &out);
    /** 主循环编辑；保存失败回滚内存，不依赖特殊库/纺织机的可用性。 */
    SysOpResult Add(SystemLang_t lang, const String &text);
    SysOpResult Update(SystemLang_t lang, const PrescriptContent &content);
    SysOpResult Delete(SystemLang_t lang, const PrescriptId &id);
    /** 同库UUID、语言和expected_revision校验后整体替换；新版本须为expected+1，失败不发布。 */
    SysOpResult ReplaceLibrary(SystemLang_t lang, const JsonDocument &document, uint32_t expected_revision);
}
