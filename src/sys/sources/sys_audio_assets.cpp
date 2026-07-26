/*
【模块职责】定义音频稳定ID、binding、/Resources运行路径和PCM常驻所有权。
【调用关系】SysRes_Init在SysAudio启动后统一调用；App仍通过AudioAssetId或现有binding播放。
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
    };
}

bool SysAudioAssets::PreloadAll()
{
    bool allReady = true;
    for (AudioAssetRecord &record : g_records)
    {
        if (!SysResourceIO::LoadWav(record.path, record.binding, record.trimLoopSilence, record.loaded))
        {
            allReady = false;
            continue;
        }
        if (!sysAudio.registerAsset(record.id, record.binding, record.loaded.clip))
        {
            Serial.printf("[音频资源] 注册失败：binding=%s。\n", record.binding);
            allReady = false;
        }
    }
    return allReady;
}
