/*
【模块职责】系统资源预热协调器，并负责双蛇杖大资源的分帧加载与复位自恢复保护。
【调用关系】AppManager调用SysRes_Init；Arduino loop调用SysRes_Update；语言切换通过SysRes_OnLanguageChanged刷新语言资源。
【重要约束】本模块不读取文件、不解析业务JSON、不暴露PSRAM裸指针；具体资源所有权由各资源域独立承担。
*/
#include "sys/sys_res.h"

#include <Arduino.h>
#include <esp_attr.h>

#include "sys/app_manager.h"
#include "sys/sys_audio_assets.h"
#include "sys/sys_caduceus_resources.h"
#include "sys/sys_coin_resources.h"
#include "sys/sys_identity_catalog.h"
#include "sys/sys_karma_resources.h"
#include "sys/sys_oracle.h"
#include "sys/sys_sea_resources.h"
#include "sys/sys_specials.h"

namespace
{
    enum class CaduceusPreloadState : uint8_t
    {
        NotScheduled = 0,
        Waiting,
        Audio,
        Images,
        Ready,
        Failed,
        Blocked,
    };

    constexpr uint32_t CADUCEUS_PRELOAD_DELAY_MS = 5000;
    constexpr uint32_t PRELOAD_GUARD_MAGIC = 0x43414455UL; // ASCII "CADU"
    constexpr uint32_t PRELOAD_GUARD_XOR = 0xA55AC33CUL;
    constexpr uint32_t CADUCEUS_PRELOAD_STEP_COUNT = 28; // 18段音频 + 10张图片。

    /*
     * RTC_NOINIT区在软件/看门狗复位时不会被C运行时清零。加载前写入、返回后清除：
     * 如果芯片在FAT读取、PSRAM写入或格式处理期间直接复位，下一次开机就能识别未完成步骤。
     * 三字段校验避免上电后未定义RTC内容偶然被当成有效保护标记。
     */
    RTC_NOINIT_ATTR volatile uint32_t g_preloadGuardMagic;
    RTC_NOINIT_ATTR volatile uint32_t g_preloadGuardStep;
    RTC_NOINIT_ATTR volatile uint32_t g_preloadGuardCheck;

    bool g_initialized = false;
    bool g_caduceusAllReady = true;
    uint32_t g_caduceusStartMs = 0;
    uint32_t g_caduceusStep = 0;
    CaduceusPreloadState g_caduceusState = CaduceusPreloadState::NotScheduled;

    void ClearPreloadGuard()
    {
        g_preloadGuardCheck = 0;
        g_preloadGuardStep = 0;
        g_preloadGuardMagic = 0;
    }

    /** 消费上次未完成步骤；返回0表示没有可信的中断标记。 */
    uint32_t ConsumeInterruptedPreloadStep()
    {
        const uint32_t step = g_preloadGuardStep;
        const bool valid = g_preloadGuardMagic == PRELOAD_GUARD_MAGIC &&
                           step >= 1 && step <= CADUCEUS_PRELOAD_STEP_COUNT &&
                           g_preloadGuardCheck == (step ^ PRELOAD_GUARD_XOR);
        ClearPreloadGuard();
        return valid ? step : 0;
    }

    void ArmPreloadGuard(uint32_t step)
    {
        // 先写步骤和校验，最后写magic；复位发生在写入中途时不会形成伪有效记录。
        g_preloadGuardStep = step;
        g_preloadGuardCheck = step ^ PRELOAD_GUARD_XOR;
        g_preloadGuardMagic = PRELOAD_GUARD_MAGIC;
    }
}

void SysRes_Init()
{
    if (g_initialized)
        return;

    Serial.println("[资源系统] 开始预加载FATFS应用资源。");
    const SystemLang_t lang = appManager.getLanguage();
    bool allReady = true;

    // 只同步加载原有基础音频/图片；双蛇杖28份大资源延迟到主循环逐份预热。
    allReady = SysAudioAssets::PreloadCore() && allReady;
    allReady = SysCoinResources::Preload() && allReady;
    allReady = SysKarmaResources::Preload() && allReady;

    // 文本资源只加载当前语言；语言切换在设置页操作期间同步重载，不把等待留到应用入口。
    allReady = SysIdentityCatalog::Load(lang) && allReady;
    sysOracle.begin();
    sysSpecials.begin();
    allReady = SysSeaResources::Load(lang) && allReady;

    const uint32_t interruptedStep = ConsumeInterruptedPreloadStep();
    if (interruptedStep != 0)
    {
        /*
         * 本次启动保持双蛇杖资源停用，但不再复位。保护标记已经消费并清除，
         * 用户下次主动重启仍可重试；当前这一次启动则始终保持安全状态。
         */
        g_caduceusState = CaduceusPreloadState::Blocked;
        Serial.printf("[资源系统] 检测到双蛇杖资源预热在步骤%lu中断，本次启动已停用该资源域以阻止连续复位。\n",
                      (unsigned long)interruptedStep);
    }
    else
    {
        g_caduceusState = CaduceusPreloadState::Waiting;
        g_caduceusStartMs = millis() + CADUCEUS_PRELOAD_DELAY_MS;
    }

    g_initialized = true;
    Serial.println(allReady ? "[资源系统] FATFS应用资源预加载完成。"
                            : "[资源系统] 资源预加载完成，但有素材缺失或无效，相关功能将使用兜底。");
}

void SysRes_Update()
{
    if (!g_initialized)
        return;

    if (g_caduceusState == CaduceusPreloadState::Waiting)
    {
        if ((int32_t)(millis() - g_caduceusStartMs) < 0)
            return;
        g_caduceusState = CaduceusPreloadState::Audio;
        Serial.println("[资源系统] 开始后台逐份预热双蛇杖资源。");
    }

    bool groupComplete = false;
    bool stepReady = true;
    if (g_caduceusState == CaduceusPreloadState::Audio)
    {
        ArmPreloadGuard(++g_caduceusStep);
        stepReady = SysAudioAssets::PreloadCaduceusStep(groupComplete);
        ClearPreloadGuard();
        g_caduceusAllReady = stepReady && g_caduceusAllReady;
        if (groupComplete)
            g_caduceusState = CaduceusPreloadState::Images;
        return;
    }

    if (g_caduceusState == CaduceusPreloadState::Images)
    {
        ArmPreloadGuard(++g_caduceusStep);
        stepReady = SysCaduceusResources::PreloadStep(groupComplete);
        ClearPreloadGuard();
        g_caduceusAllReady = stepReady && g_caduceusAllReady;
        if (!groupComplete)
            return;

        g_caduceusState = g_caduceusAllReady ? CaduceusPreloadState::Ready
                                             : CaduceusPreloadState::Failed;
        Serial.println(g_caduceusAllReady
                           ? "[资源系统] 双蛇杖音频和图片后台预热完成。"
                           : "[资源系统] 双蛇杖资源预热完成，但有素材缺失或无效。");
    }
}

void SysRes_RequestCaduceusPreload()
{
    if (g_caduceusState == CaduceusPreloadState::Waiting)
        g_caduceusStartMs = millis();
}

bool SysRes_IsCaduceusReady()
{
    return g_caduceusState == CaduceusPreloadState::Ready;
}

bool SysRes_IsCaduceusUnavailable()
{
    return g_caduceusState == CaduceusPreloadState::Failed ||
           g_caduceusState == CaduceusPreloadState::Blocked;
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
