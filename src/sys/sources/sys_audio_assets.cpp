/*
【模块职责】定义音频稳定ID、binding、/Resources运行路径和PCM常驻所有权。
【调用关系】SysRes_Init同步加载基础音频，SysRes_Update逐份加载双蛇杖音频；App仍通过AudioAssetId播放。
*/
#include "sys/sys_audio_assets.h"

#include "sys/sys_audio.h"
#include "sys/sys_constants.h"
#include "sys/sys_resource_io.h"

namespace
{
    struct AudioAssetRecord
    {
        AudioAssetId id;
        const char *binding;
        SysResourcePath path;
        bool trimLoopSilence;
        SysLoadedAudioAsset loaded;
    };

    AudioAssetRecord g_records[] = {
        {AudioAssetId::Procedure, "procedure",
         {PrescriptConst::AUDIO_PROCEDURE_WAV}, true, {}},
        {AudioAssetId::Final, "final",
         {PrescriptConst::AUDIO_FINAL_WAV}, false, {}},
        {AudioAssetId::CoinHeads, "heads",
         {PrescriptConst::AUDIO_COIN_HEADS_WAV}, false, {}},
        {AudioAssetId::CoinTails, "tails",
         {PrescriptConst::AUDIO_COIN_TAILS_WAV}, false, {}},
        {AudioAssetId::Ahab, "Ahab",
         {PrescriptConst::AUDIO_AHAB_WAV}, false, {}},
        {AudioAssetId::SeaRain, "sea.rain",
         {PrescriptConst::AUDIO_SEA_RAIN_WAV}, true, {}},
        {AudioAssetId::Karma1, "karma.muyu1",
         {PrescriptConst::AUDIO_KARMA_1_WAV}, false, {}},
        {AudioAssetId::Karma2, "karma.muyu2",
         {PrescriptConst::AUDIO_KARMA_2_WAV}, false, {}},
        {AudioAssetId::Karma3, "karma.muyu3",
         {PrescriptConst::AUDIO_KARMA_3_WAV}, false, {}},

        // 双蛇杖音频按真实文件逐一注册；武器配置表只引用ID，因此多个武器复用音效时不会复制PCM。
        {AudioAssetId::CaduceusChange1, "furioso.change_weapon1",
         {"/Resources/furioso/audio/change_sound/change_weapon1.wav"}, false, {}},
        {AudioAssetId::CaduceusChange2, "furioso.change_weapon2",
         {"/Resources/furioso/audio/change_sound/change_weapon2.wav"}, false, {}},
        {AudioAssetId::CaduceusChange3, "furioso.change_weapon3",
         {"/Resources/furioso/audio/change_sound/change_weapon3.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst1, "furioso.first_complete.first",
         {"/Resources/furioso/audio/first_complete/first.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst2, "furioso.first_complete.second",
         {"/Resources/furioso/audio/first_complete/second.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst3, "furioso.first_complete.third",
         {"/Resources/furioso/audio/first_complete/third.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst4, "furioso.first_complete.forth",
         {"/Resources/furioso/audio/first_complete/forth.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst5, "furioso.first_complete.fifth",
         {"/Resources/furioso/audio/first_complete/fifth.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst6, "furioso.first_complete.sixth",
         {"/Resources/furioso/audio/first_complete/sixth.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst7, "furioso.first_complete.seventh",
         {"/Resources/furioso/audio/first_complete/seventh.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst8, "furioso.first_complete.eighth",
         {"/Resources/furioso/audio/first_complete/eighth.wav"}, false, {}},
        {AudioAssetId::CaduceusFirst9, "furioso.first_complete.ninth",
         {"/Resources/furioso/audio/first_complete/ninth.wav"}, false, {}},
        {AudioAssetId::CaduceusEffectGiantSword, "furioso.effect.giant_sword",
         {"/Resources/furioso/audio/sound_effect/giant_sword.wav"}, false, {}},
        {AudioAssetId::CaduceusEffectScythe, "furioso.effect.scythe",
         {"/Resources/furioso/audio/sound_effect/scythe.wav"}, false, {}},
        {AudioAssetId::CaduceusEffectSword1, "furioso.effect.sword1",
         {"/Resources/furioso/audio/sound_effect/sword.wav"}, false, {}},
        {AudioAssetId::CaduceusEffectSword2, "furioso.effect.sword2",
         {"/Resources/furioso/audio/sound_effect/sword2.wav"}, false, {}},
        {AudioAssetId::CaduceusEffectSword3, "furioso.effect.sword3",
         {"/Resources/furioso/audio/sound_effect/sword3.wav"}, false, {}},
        {AudioAssetId::CaduceusEffectWhip, "furioso.effect.whip",
         {"/Resources/furioso/audio/sound_effect/whip.wav"}, false, {}},
    };

    // Invalid不占资源记录；其余稳定ID必须各有且仅有一份真实PCM绑定。
    static_assert(sizeof(g_records) / sizeof(g_records[0]) ==
                      (size_t)AudioAssetId::Count - 1U,
                  "音频资源表必须覆盖全部有效AudioAssetId");

    constexpr size_t CADUCEUS_FIRST_RECORD = (size_t)AudioAssetId::CaduceusChange1 - 1U;
    constexpr size_t AUDIO_RECORD_COUNT = sizeof(g_records) / sizeof(g_records[0]);
    size_t g_nextCaduceusRecord = CADUCEUS_FIRST_RECORD;

    /** 加载并注册一条记录，统一基础预热和延迟预热的错误处理。 */
    bool LoadAndRegister(AudioAssetRecord &record)
    {
        if (!SysResourceIO::LoadWav(record.path, record.binding, record.trimLoopSilence, record.loaded))
            return false;
        if (!sysAudio.registerAsset(record.id, record.binding, record.loaded.clip))
        {
            Serial.printf("[音频资源] 注册失败：binding=%s。\n", record.binding);
            return false;
        }
        return true;
    }
}

bool SysAudioAssets::PreloadCore()
{
    bool allReady = true;
    for (size_t index = 0; index < CADUCEUS_FIRST_RECORD; ++index)
    {
        if (!LoadAndRegister(g_records[index]))
            allReady = false;
    }
    return allReady;
}

bool SysAudioAssets::PreloadCaduceusStep(bool &complete)
{
    if (g_nextCaduceusRecord >= AUDIO_RECORD_COUNT)
    {
        complete = true;
        return true;
    }

    const bool ready = LoadAndRegister(g_records[g_nextCaduceusRecord]);
    ++g_nextCaduceusRecord;
    complete = g_nextCaduceusRecord >= AUDIO_RECORD_COUNT;
    return ready;
}

void SysAudioAssets::ReleaseCaduceus()
{
    /*
     * 先撤销AudioAssetId到PCM的公开映射，再释放资源域持有的PSRAM。
     * App已经用SysAudio::stopAndWait建立跨核心边界，这里不再反向管理播放实例。
     */
    for (size_t index = CADUCEUS_FIRST_RECORD; index < AUDIO_RECORD_COUNT; ++index)
    {
        (void)sysAudio.unregisterAsset(g_records[index].id);
        g_records[index].loaded.reset();
    }
    g_nextCaduceusRecord = CADUCEUS_FIRST_RECORD;
}
