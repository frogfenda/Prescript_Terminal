/*
【模块职责】prescript 功能域的固定双语 UI 文案实现。
【边界】本文件只保存静态 UI 文本和轻量选择逻辑，不读取文件、不保存语言状态。
*/
#include "lang/ui_strings.h"

namespace UIStrings
{
    const char *UserTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "使用者" : "USER";
    }

    const char *ClearCurrentUserItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "清空当前使用者" : "CLEAR CURRENT USER";
    }

    const char *CurrentUserSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? " < 当前" : " < CURRENT";
    }

    const char *DangerTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "危险操作" : "DANGER";
    }

    const char *DeleteUserHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按删除 / 长按取消" : "CLICK DELETE / HOLD CANCEL";
    }

    // 【指令档案】档案浏览页的空状态、底部提示和删除流程文本。
    const char *ArchiveDeleteTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "确认删除此指令？" : "DELETE THIS RECORD?";
    }

    const char *ArchiveEmpty(SystemLang_t lang)
    {
        return IsZh(lang) ? "暂无指令档案" : "NO ARCHIVED PRESCRIPTS";
    }

    const char *ClickBackHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按返回" : "CLICK BACK";
    }

    const char *HoldBackHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按返回" : "HOLD BACK";
    }

    const char *ClickDeleteHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按删除" : "CLICK DELETE";
    }

    const char *HoldExitHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按退出" : "HOLD EXIT";
    }

    const char *PurgingRecord(SystemLang_t lang)
    {
        return IsZh(lang) ? "正在删除指令..." : "DELETING RECORD...";
    }

    // 【时间设置】时间/日期链路编辑页与立即网络校时入口。月份/日期后缀也从这里分语言取值。
}
