/*
【模块职责】系统资源预热协调器。
【调用关系】AppManager在音频任务和FATFS都就绪后调用SysRes_Init；语言切换通过SysRes_OnLanguageChanged统一刷新语言资源。
【重要约束】本模块不读取文件、不解析业务JSON、不暴露PSRAM裸指针；具体资源所有权由各资源域独立承担。
*/
#include "sys/sys_res.h"

#include "sys/app_manager.h"
#include "sys/sys_audio_assets.h"
#include "sys/sys_coin_resources.h"
#include "sys/sys_identity_catalog.h"
#include "sys/sys_karma_resources.h"
#include "sys/sys_oracle.h"
#include "sys/sys_sea_resources.h"
#include "sys/sys_specials.h"

namespace
{
    bool g_initialized = false;
}

void SysRes_Init()
{
    if (g_initialized)
        return;

    Serial.println("[资源系统] 开始预加载FATFS应用资源。");
    const SystemLang_t lang = appManager.getLanguage();
    bool allReady = true;

    // 音频和图片没有语言差异，全部在开机阶段常驻，应用打开时不再发生FAT读取。
    allReady = SysAudioAssets::PreloadAll() && allReady;
    allReady = SysCoinResources::Preload() && allReady;
    allReady = SysKarmaResources::Preload() && allReady;

    // 文本资源只加载当前语言；语言切换在设置页操作期间同步重载，不把等待留到应用入口。
    allReady = SysIdentityCatalog::Load(lang) && allReady;
    sysOracle.begin();
    sysSpecials.begin();
    allReady = SysSeaResources::Load(lang) && allReady;

    g_initialized = true;
    Serial.println(allReady ? "[资源系统] FATFS应用资源预加载完成。"
                            : "[资源系统] 资源预加载完成，但有素材缺失或无效，相关功能将使用兜底。");
}

void SysRes_OnLanguageChanged(SystemLang_t lang)
{
    if (!g_initialized)
    {
        SysRes_Init();
        return;
    }

    Serial.printf("[资源系统] 正在预加载%s语言资源。\n", TerminalLang::DisplayName(lang, LANG_ZH));
    (void)SysIdentityCatalog::Load(lang);
    sysOracle.begin();
    sysSpecials.begin();
    (void)SysSeaResources::Load(lang);
    Serial.printf("[资源系统] %s语言资源切换完成。\n", TerminalLang::DisplayName(lang, LANG_ZH));
}
