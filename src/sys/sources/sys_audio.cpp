// 文件：src/sys/sources/sys_audio.cpp
// 职责：在 Core 0 后台任务中统一管理资源播放、程序音、多路混音、循环和淡入淡出。
#include "sys/sys_audio.h"
#include "sys/sys_config.h"
#include "bsp/bsp_audio_i2s.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

SysAudio sysAudio;

namespace
{
constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
constexpr int AUDIO_CHUNK_FRAMES = 128;
constexpr int AUDIO_I2S_SAMPLES = AUDIO_CHUNK_FRAMES * 2;
constexpr int AUDIO_PCM_VOICE_COUNT = 6;
constexpr int AUDIO_TONE_VOICE_COUNT = 4;
constexpr int AUDIO_COMMAND_QUEUE_LEN = 24;

// 保留原系统的平方音量曲线；多路相加后的峰值会在最终输出块统一限幅。
constexpr float AUDIO_MASTER_GAIN = 1.45f;
constexpr float AUDIO_OUTPUT_PEAK = 30000.0f;

constexpr EventBits_t AUDIO_EVENT_SUSPENDED = BIT0;

enum class AudioCommandType : uint8_t
{
    PlayPcm,
    StopHandle,
    StopBus,
    SetGain,
    SetBusGain,
    PlayTone,
    PlayGlitch,
    Suspend,
    Resume
};

struct AudioCommand
{
    AudioCommandType type = AudioCommandType::PlayPcm;
    AudioHandle handle = AUDIO_HANDLE_INVALID;
    AudioClip clip;
    AudioPlayOptions options;
    AudioBus bus = AudioBus::Effect;
    float gain = 1.0f;
    uint16_t fadeMs = 0;
    uint16_t frequency = 0;
    uint16_t durationMs = 0;
    uint16_t delayMs = 0;
    float startFrequency = 0.0f;
    float endFrequency = 0.0f;
};

struct AssetEntry
{
    bool registered = false;
    // 固定数组由音频注册表自己持有，调用方传入临时 String::c_str() 也不会留下悬空指针。
    char binding[64] = {0};
    AudioClip clip;
};

struct GainRamp
{
    float current = 1.0f;
    float target = 1.0f;
    float step = 0.0f;
    uint32_t framesRemaining = 0;
};

struct PcmVoice
{
    bool active = false;
    bool releaseWhenSilent = false;
    AudioHandle handle = AUDIO_HANDLE_INVALID;
    AudioClip clip;
    AudioBus bus = AudioBus::Effect;
    AudioLoopMode loopMode = AudioLoopMode::None;
    uint32_t frameIndex = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    uint32_t crossfadeFrames = 0;
    GainRamp gain;
};

enum class ToneType : uint8_t
{
    Tone,
    Glitch
};

struct ToneVoice
{
    bool active = false;
    ToneType type = ToneType::Tone;
    uint32_t totalFrames = 0;
    uint32_t frameIndex = 0;
    uint32_t delayFrames = 0;
    uint16_t frequency = 0;
    float startFrequency = 0.0f;
    float endFrequency = 0.0f;
    float phase = 0.0f;
    float amplitude = 0.0f;
};

QueueHandle_t g_commandQueue = nullptr;
SemaphoreHandle_t g_pcmSlotSemaphore = nullptr;
EventGroupHandle_t g_audioEvents = nullptr;
TaskHandle_t g_audioTask = nullptr;

AssetEntry g_assets[(size_t)AudioAssetId::Count];
portMUX_TYPE g_assetMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_handleMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_playingHandleMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_nextHandle = 1;
AudioHandle g_playingHandles[AUDIO_PCM_VOICE_COUNT] = {};

// 兼容接口只管理自己创建的实例，不能误停新引擎中的环境音、对白或 UI 音。
AudioHandle g_legacyHandle = AUDIO_HANDLE_INVALID;

float clampGain(float gain)
{
    if (gain < 0.0f)
        return 0.0f;
    if (gain > 2.0f)
        return 2.0f;
    return gain;
}

bool validBus(AudioBus bus)
{
    return (uint8_t)bus < (uint8_t)AudioBus::Count;
}

bool validClip(const AudioClip &clip)
{
    return clip.samples != nullptr &&
           clip.frameCount > 0 &&
           clip.sampleRate == AUDIO_SAMPLE_RATE;
}

float currentMasterGain()
{
    float ratio = (float)sysConfig.volume / 100.0f;
    return ratio * ratio * AUDIO_MASTER_GAIN;
}

AudioHandle allocateHandle()
{
    portENTER_CRITICAL(&g_handleMux);
    AudioHandle handle = g_nextHandle++;
    if (handle == AUDIO_HANDLE_INVALID)
        handle = g_nextHandle++;
    portEXIT_CRITICAL(&g_handleMux);
    return handle;
}

/**
 * 在播放命令进入队列前登记句柄。
 * 句柄表容量与PCM槽信号量完全一致，因此正常情况下预约到信号量后必然能登记；
 * 单独保留失败返回是为了在内部状态异常时归还预约，而不是让App永久等待一个幽灵句柄。
 */
bool markHandlePlaying(AudioHandle handle)
{
    if (handle == AUDIO_HANDLE_INVALID)
        return false;
    bool marked = false;
    portENTER_CRITICAL(&g_playingHandleMux);
    for (AudioHandle &entry : g_playingHandles)
    {
        if (entry == AUDIO_HANDLE_INVALID)
        {
            entry = handle;
            marked = true;
            break;
        }
    }
    portEXIT_CRITICAL(&g_playingHandleMux);
    return marked;
}

void markHandleFinished(AudioHandle handle)
{
    if (handle == AUDIO_HANDLE_INVALID)
        return;
    portENTER_CRITICAL(&g_playingHandleMux);
    for (AudioHandle &entry : g_playingHandles)
    {
        if (entry == handle)
        {
            entry = AUDIO_HANDLE_INVALID;
            break;
        }
    }
    portEXIT_CRITICAL(&g_playingHandleMux);
}

bool handleIsPlaying(AudioHandle handle)
{
    if (handle == AUDIO_HANDLE_INVALID)
        return false;
    bool playing = false;
    portENTER_CRITICAL(&g_playingHandleMux);
    for (const AudioHandle entry : g_playingHandles)
    {
        if (entry == handle)
        {
            playing = true;
            break;
        }
    }
    portEXIT_CRITICAL(&g_playingHandleMux);
    return playing;
}

bool enqueueCommand(const AudioCommand &command, TickType_t waitTicks = 0)
{
    return g_commandQueue != nullptr && xQueueSend(g_commandQueue, &command, waitTicks) == pdTRUE;
}

void configureRamp(GainRamp &ramp, float target, uint16_t fadeMs)
{
    target = clampGain(target);
    uint32_t frames = ((uint32_t)fadeMs * AUDIO_SAMPLE_RATE) / 1000U;
    if (frames == 0)
    {
        ramp.current = target;
        ramp.target = target;
        ramp.step = 0.0f;
        ramp.framesRemaining = 0;
        return;
    }

    ramp.target = target;
    ramp.framesRemaining = frames;
    ramp.step = (target - ramp.current) / (float)frames;
}

void advanceRamp(GainRamp &ramp)
{
    if (ramp.framesRemaining == 0)
        return;

    ramp.current += ramp.step;
    ramp.framesRemaining--;
    if (ramp.framesRemaining == 0)
    {
        ramp.current = ramp.target;
        ramp.step = 0.0f;
    }
}

int32_t readPcmFrame(const AudioClip &clip, uint32_t frame)
{
    return clip.samples[frame];
}

void releasePcmVoice(PcmVoice &voice)
{
    if (!voice.active)
        return;

    markHandleFinished(voice.handle);
    voice = PcmVoice{};
    if (g_pcmSlotSemaphore != nullptr)
        xSemaphoreGive(g_pcmSlotSemaphore);
}

void startPcmVoice(PcmVoice &voice, const AudioCommand &command)
{
    voice = PcmVoice{};
    voice.active = true;
    voice.handle = command.handle;
    voice.clip = command.clip;
    voice.bus = validBus(command.options.bus) ? command.options.bus : AudioBus::Effect;
    voice.loopMode = command.options.loopMode;
    voice.loopStart = command.clip.loopStartFrame;
    voice.loopEnd = command.clip.loopEndFrame == 0
                        ? command.clip.frameCount
                        : min(command.clip.loopEndFrame, command.clip.frameCount);

    if (voice.loopStart >= voice.loopEnd)
    {
        voice.loopStart = 0;
        voice.loopEnd = command.clip.frameCount;
    }

    uint32_t loopFrames = voice.loopEnd - voice.loopStart;
    uint32_t requestedCrossfade = ((uint32_t)command.options.crossfadeMs * AUDIO_SAMPLE_RATE) / 1000U;
    voice.crossfadeFrames = min(requestedCrossfade, loopFrames / 2U);
    if (voice.loopMode == AudioLoopMode::Crossfade && voice.crossfadeFrames == 0)
        voice.loopMode = AudioLoopMode::Exact;

    voice.gain.current = command.options.fadeInMs > 0 ? 0.0f : clampGain(command.options.gain);
    voice.gain.target = voice.gain.current;
    if (command.options.fadeInMs > 0)
        configureRamp(voice.gain, command.options.gain, command.options.fadeInMs);
}

void stopPcmVoice(PcmVoice &voice, uint16_t fadeMs)
{
    if (!voice.active)
        return;
    if (fadeMs == 0 || voice.gain.current <= 0.0001f)
    {
        releasePcmVoice(voice);
        return;
    }

    voice.releaseWhenSilent = true;
    configureRamp(voice.gain, 0.0f, fadeMs);
}

bool mixPcmVoice(PcmVoice &voice, const GainRamp &busGain, int32_t &mixSample)
{
    if (!voice.active)
        return false;

    uint32_t playbackEnd = voice.loopMode == AudioLoopMode::None ? voice.clip.frameCount : voice.loopEnd;
    if (voice.frameIndex >= playbackEnd)
    {
        if (voice.loopMode == AudioLoopMode::None)
        {
            releasePcmVoice(voice);
            return false;
        }
        voice.frameIndex = voice.loopStart;
    }

    int32_t sample = readPcmFrame(voice.clip, voice.frameIndex);

    // 交叉淡化只发生在循环边界附近。混完尾部和头部后直接跳过已经混入的开头帧，
    // 因而不会重复播放交叉区，也不会像旧实现那样每轮制造一次音量凹口。
    if (voice.loopMode == AudioLoopMode::Crossfade && voice.crossfadeFrames > 0)
    {
        uint32_t crossfadeStart = voice.loopEnd - voice.crossfadeFrames;
        if (voice.frameIndex >= crossfadeStart)
        {
            uint32_t offset = voice.frameIndex - crossfadeStart;
            uint32_t headFrame = voice.loopStart + offset;
            const int32_t headSample = readPcmFrame(voice.clip, headFrame);
            float blend = (float)(offset + 1U) / (float)voice.crossfadeFrames;
            sample = (int32_t)((float)sample * (1.0f - blend) + (float)headSample * blend);
        }
    }

    float voiceGain = voice.gain.current * busGain.current;
    mixSample += (int32_t)((float)sample * voiceGain);

    voice.frameIndex++;
    if (voice.frameIndex >= playbackEnd)
    {
        if (voice.loopMode == AudioLoopMode::Exact)
        {
            voice.frameIndex = voice.loopStart;
        }
        else if (voice.loopMode == AudioLoopMode::Crossfade)
        {
            voice.frameIndex = voice.loopStart + voice.crossfadeFrames;
            if (voice.frameIndex >= voice.loopEnd)
                voice.frameIndex = voice.loopStart;
        }
        else
        {
            releasePcmVoice(voice);
            return true;
        }
    }

    advanceRamp(voice.gain);
    if (voice.releaseWhenSilent && voice.gain.framesRemaining == 0 && voice.gain.current <= 0.0001f)
        releasePcmVoice(voice);
    return true;
}

void startToneVoice(ToneVoice &voice, const AudioCommand &command)
{
    voice = ToneVoice{};
    voice.active = true;
    voice.type = command.type == AudioCommandType::PlayGlitch ? ToneType::Glitch : ToneType::Tone;
    voice.totalFrames = ((uint32_t)command.durationMs * AUDIO_SAMPLE_RATE) / 1000U;
    if (voice.totalFrames == 0)
        voice.totalFrames = 1;
    voice.delayFrames = ((uint32_t)command.delayMs * AUDIO_SAMPLE_RATE) / 1000U;
    voice.frequency = command.frequency;
    voice.startFrequency = command.startFrequency;
    voice.endFrequency = command.endFrequency;
    voice.amplitude = voice.type == ToneType::Glitch ? 10000.0f : 12000.0f;
    if (voice.type == ToneType::Tone && voice.frequency < 1500)
        voice.amplitude *= 0.5f;
}

int32_t nextToneSample(ToneVoice &voice)
{
    if (!voice.active || voice.frameIndex >= voice.totalFrames)
    {
        voice.active = false;
        return 0;
    }

    if (voice.delayFrames > 0)
    {
        voice.delayFrames--;
        return 0;
    }

    float progress = (float)voice.frameIndex / (float)voice.totalFrames;
    float sample = 0.0f;
    if (voice.type == ToneType::Glitch)
    {
        float frequency = voice.startFrequency - (voice.startFrequency - voice.endFrequency) * progress;
        voice.phase += frequency / (float)AUDIO_SAMPLE_RATE;
        while (voice.phase > 1.0f)
            voice.phase -= 1.0f;
        float wave = 4.0f * fabsf(voice.phase - 0.5f) - 1.0f;
        float envelope = (1.0f - progress) * (1.0f - progress);
        sample = wave * voice.amplitude * envelope;
    }
    else
    {
        if (voice.frequency == 0)
        {
            voice.active = false;
            return 0;
        }
        float period = (float)AUDIO_SAMPLE_RATE / (float)voice.frequency;
        float phase = fmodf((float)voice.frameIndex, period) / period;
        float duty = voice.frequency < 1500 ? 0.25f : 0.5f;
        float wave = phase < duty ? 1.0f : -1.0f;
        float envelope = (1.0f - progress) * (1.0f - progress);
        sample = wave * voice.amplitude * envelope;
    }

    voice.frameIndex++;
    if (voice.frameIndex >= voice.totalFrames)
        voice.active = false;
    return (int32_t)sample;
}

void processCommand(const AudioCommand &command,
                    PcmVoice *pcmVoices,
                    ToneVoice *toneVoices,
                    GainRamp *busGains,
                    bool &suspended)
{
    switch (command.type)
    {
    case AudioCommandType::PlayPcm:
        for (int i = 0; i < AUDIO_PCM_VOICE_COUNT; ++i)
        {
            if (!pcmVoices[i].active)
            {
                startPcmVoice(pcmVoices[i], command);
                return;
            }
        }
        // 正常情况下计数信号量保证一定有槽；若状态异常则归还预约，避免永久耗尽。
        if (g_pcmSlotSemaphore != nullptr)
            xSemaphoreGive(g_pcmSlotSemaphore);
        markHandleFinished(command.handle);
        Serial.println("[音频] PCM 槽预约与任务状态不一致，本次播放已取消。");
        return;

    case AudioCommandType::StopHandle:
        for (int i = 0; i < AUDIO_PCM_VOICE_COUNT; ++i)
            if (pcmVoices[i].active && pcmVoices[i].handle == command.handle)
                stopPcmVoice(pcmVoices[i], command.fadeMs);
        return;

    case AudioCommandType::StopBus:
        for (int i = 0; i < AUDIO_PCM_VOICE_COUNT; ++i)
            if (pcmVoices[i].active && pcmVoices[i].bus == command.bus)
                stopPcmVoice(pcmVoices[i], command.fadeMs);
        // 程序音固定属于 Ui 总线；它没有长期句柄，停止 Ui 时直接清掉全部短音槽。
        if (command.bus == AudioBus::Ui)
            for (int i = 0; i < AUDIO_TONE_VOICE_COUNT; ++i)
                toneVoices[i].active = false;
        return;

    case AudioCommandType::SetGain:
        for (int i = 0; i < AUDIO_PCM_VOICE_COUNT; ++i)
            if (pcmVoices[i].active && pcmVoices[i].handle == command.handle)
                configureRamp(pcmVoices[i].gain, command.gain, command.fadeMs);
        return;

    case AudioCommandType::SetBusGain:
        if (validBus(command.bus))
            configureRamp(busGains[(uint8_t)command.bus], command.gain, command.fadeMs);
        return;

    case AudioCommandType::PlayTone:
    case AudioCommandType::PlayGlitch:
        for (int i = 0; i < AUDIO_TONE_VOICE_COUNT; ++i)
        {
            if (!toneVoices[i].active)
            {
                startToneVoice(toneVoices[i], command);
                return;
            }
        }
        // UI 快速滚动时宁可丢掉最新短音，也不把短音排队到操作结束以后播放。
        return;

    case AudioCommandType::Suspend:
        suspended = true;
        if (g_audioEvents != nullptr)
            xEventGroupSetBits(g_audioEvents, AUDIO_EVENT_SUSPENDED);
        return;

    case AudioCommandType::Resume:
        suspended = false;
        if (g_audioEvents != nullptr)
            xEventGroupClearBits(g_audioEvents, AUDIO_EVENT_SUSPENDED);
        return;
    }
}

/**
 * Core 0 音频任务。
 *
 * 每次先消费所有控制命令，再生成 128 帧：所有 PCM Voice 和程序音先累加到 int32 块，
 * 应用实例/总线/系统音量后做整块峰值限幅，最后才转换成 int16 写入唯一的 I2S0。
 */
void audioTask(void *)
{
    PcmVoice pcmVoices[AUDIO_PCM_VOICE_COUNT];
    ToneVoice toneVoices[AUDIO_TONE_VOICE_COUNT];
    GainRamp busGains[(size_t)AudioBus::Count];
    // 业务混音只保留单声道；最终写I2S前再复制到左右时隙，避免双份32位累加缓冲占用任务栈。
    int32_t mixBuffer[AUDIO_CHUNK_FRAMES];
    int16_t outputBuffer[AUDIO_I2S_SAMPLES];
    bool suspended = false;

    busGains[(uint8_t)AudioBus::Ambient].current = 0.50f;
    busGains[(uint8_t)AudioBus::Voice].current = 1.00f;
    busGains[(uint8_t)AudioBus::Effect].current = 0.85f;
    busGains[(uint8_t)AudioBus::Ui].current = 0.65f;
    for (size_t i = 0; i < (size_t)AudioBus::Count; ++i)
        busGains[i].target = busGains[i].current;

    while (true)
    {
        AudioCommand command;
        while (g_commandQueue != nullptr && xQueueReceive(g_commandQueue, &command, 0) == pdTRUE)
            processCommand(command, pcmVoices, toneVoices, busGains, suspended);

        if (suspended)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        memset(mixBuffer, 0, sizeof(mixBuffer));
        bool anyActive = false;

        for (int frame = 0; frame < AUDIO_CHUNK_FRAMES; ++frame)
        {
            int32_t sample = 0;

            for (int i = 0; i < AUDIO_PCM_VOICE_COUNT; ++i)
            {
                if (!pcmVoices[i].active)
                    continue;
                const GainRamp &busGain = busGains[(uint8_t)pcmVoices[i].bus];
                mixPcmVoice(pcmVoices[i], busGain, sample);
                anyActive = true;
            }

            for (int i = 0; i < AUDIO_TONE_VOICE_COUNT; ++i)
            {
                if (!toneVoices[i].active)
                    continue;
                // 每个Tone Voice在每帧只能推进一次；重复调用会令相位和持续时间加速一倍。
                const int32_t toneSample = nextToneSample(toneVoices[i]);
                const float uiGain = busGains[(uint8_t)AudioBus::Ui].current;
                sample += (int32_t)((float)toneSample * uiGain);
                anyActive = true;
            }

            mixBuffer[frame] = sample;
            for (size_t bus = 0; bus < (size_t)AudioBus::Count; ++bus)
                advanceRamp(busGains[bus]);
        }

        if (!anyActive)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        float masterGain = currentMasterGain();
        float peak = 0.0f;
        for (int i = 0; i < AUDIO_CHUNK_FRAMES; ++i)
        {
            float value = fabsf((float)mixBuffer[i] * masterGain);
            if (value > peak)
                peak = value;
        }

        float limiter = peak > AUDIO_OUTPUT_PEAK ? AUDIO_OUTPUT_PEAK / peak : 1.0f;
        for (int i = 0; i < AUDIO_CHUNK_FRAMES; ++i)
        {
            float value = (float)mixBuffer[i] * masterGain * limiter;
            if (value > 32767.0f)
                value = 32767.0f;
            else if (value < -32768.0f)
                value = -32768.0f;
            const int16_t mono = (int16_t)value;
            outputBuffer[i * 2] = mono;
            outputBuffer[i * 2 + 1] = mono;
        }

        if (masterGain <= 0.0f)
        {
            // 静音时仍按真实时间推进 Voice，但不向 DMA 连续灌入全零块。
            vTaskDelay(pdMS_TO_TICKS(3));
        }
        else if (!BSP::AudioI2S::Write(outputBuffer, AUDIO_I2S_SAMPLES, portMAX_DELAY))
        {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

bool copyAsset(AudioAssetId id, AudioClip &outClip)
{
    size_t index = (size_t)id;
    if (index == 0 || index >= (size_t)AudioAssetId::Count)
        return false;

    portENTER_CRITICAL(&g_assetMux);
    bool found = g_assets[index].registered;
    if (found)
        outClip = g_assets[index].clip;
    portEXIT_CRITICAL(&g_assetMux);
    return found;
}

bool copyAsset(const char *binding, AudioClip &outClip)
{
    if (!binding || binding[0] == '\0')
        return false;

    for (size_t i = 1; i < (size_t)AudioAssetId::Count; ++i)
    {
        char entryBinding[sizeof(g_assets[i].binding)];
        portENTER_CRITICAL(&g_assetMux);
        bool registered = g_assets[i].registered;
        memcpy(entryBinding, g_assets[i].binding, sizeof(entryBinding));
        AudioClip clip = g_assets[i].clip;
        portEXIT_CRITICAL(&g_assetMux);

        if (registered && entryBinding[0] != '\0' && strcmp(entryBinding, binding) == 0)
        {
            outClip = clip;
            return true;
        }
    }
    return false;
}

} // namespace

void SysAudio::begin()
{
    if (g_audioTask != nullptr)
        return;

    if (g_commandQueue == nullptr)
        g_commandQueue = xQueueCreate(AUDIO_COMMAND_QUEUE_LEN, sizeof(AudioCommand));
    if (g_pcmSlotSemaphore == nullptr)
        g_pcmSlotSemaphore = xSemaphoreCreateCounting(AUDIO_PCM_VOICE_COUNT, AUDIO_PCM_VOICE_COUNT);
    if (g_audioEvents == nullptr)
        g_audioEvents = xEventGroupCreate();

    if (!g_commandQueue || !g_pcmSlotSemaphore || !g_audioEvents)
    {
        Serial.println("[音频] 音频任务同步对象创建失败。");
        return;
    }

    if (!BSP::AudioI2S::Begin(AUDIO_SAMPLE_RATE))
        return;

    if (g_audioTask == nullptr)
    {
        BaseType_t created = xTaskCreatePinnedToCore(audioTask, "SysAudio_Task", 6144, nullptr, 1, &g_audioTask, 0);
        if (created != pdPASS)
        {
            g_audioTask = nullptr;
            Serial.println("[音频] Core 0 音频任务创建失败。");
        }
    }
}

bool SysAudio::registerAsset(AudioAssetId id, const char *binding, const AudioClip &clip)
{
    size_t index = (size_t)id;
    if (index == 0 || index >= (size_t)AudioAssetId::Count || !validClip(clip))
        return false;
    if (binding && strnlen(binding, sizeof(g_assets[index].binding)) >= sizeof(g_assets[index].binding))
        return false;

    portENTER_CRITICAL(&g_assetMux);
    g_assets[index].registered = true;
    g_assets[index].binding[0] = '\0';
    if (binding)
    {
        strncpy(g_assets[index].binding, binding, sizeof(g_assets[index].binding) - 1U);
        g_assets[index].binding[sizeof(g_assets[index].binding) - 1U] = '\0';
    }
    g_assets[index].clip = clip;
    portEXIT_CRITICAL(&g_assetMux);
    return true;
}

bool SysAudio::unregisterAsset(AudioAssetId id)
{
    const size_t index = (size_t)id;
    if (index == 0 || index >= (size_t)AudioAssetId::Count)
        return false;

    /*
     * 注册表只保存PCM只读视图，不拥有内存。资源域会在本函数返回后释放PCM，
     * 因此调用方必须事先通过stopAndWait确认后台Voice已经清除自己的视图副本。
     */
    portENTER_CRITICAL(&g_assetMux);
    g_assets[index] = AssetEntry{};
    portEXIT_CRITICAL(&g_assetMux);
    return true;
}

bool SysAudio::hasAsset(AudioAssetId id) const
{
    AudioClip clip;
    return copyAsset(id, clip);
}

AudioHandle SysAudio::play(AudioAssetId id, const AudioPlayOptions &options)
{
    AudioClip clip;
    return copyAsset(id, clip) ? play(clip, options) : AUDIO_HANDLE_INVALID;
}

AudioHandle SysAudio::play(const char *binding, const AudioPlayOptions &options)
{
    AudioClip clip;
    return copyAsset(binding, clip) ? play(clip, options) : AUDIO_HANDLE_INVALID;
}

AudioHandle SysAudio::play(const AudioClip &clip, const AudioPlayOptions &options)
{
    if (!validClip(clip) || !validBus(options.bus) || g_pcmSlotSemaphore == nullptr)
        return AUDIO_HANDLE_INVALID;

    // 信号量在命令入队前预约一个真实 PCM 槽，因此返回有效句柄就代表任务端能够接纳该 Voice。
    if (xSemaphoreTake(g_pcmSlotSemaphore, 0) != pdTRUE)
        return AUDIO_HANDLE_INVALID;

    AudioCommand command;
    command.type = AudioCommandType::PlayPcm;
    command.handle = allocateHandle();
    command.clip = clip;
    command.options = options;
    command.options.gain = clampGain(options.gain);

    /*
     * 先登记再入队，使isPlaying()从play()返回的第一刻就能看见“待播放”状态；
     * 若先入队，极短PCM可能在主线程登记前已经被后台播放并释放，留下永不结束的假状态。
     */
    if (!markHandlePlaying(command.handle))
    {
        xSemaphoreGive(g_pcmSlotSemaphore);
        return AUDIO_HANDLE_INVALID;
    }

    if (!enqueueCommand(command))
    {
        markHandleFinished(command.handle);
        xSemaphoreGive(g_pcmSlotSemaphore);
        return AUDIO_HANDLE_INVALID;
    }
    return command.handle;
}

bool SysAudio::isPlaying(AudioHandle handle) const
{
    return handleIsPlaying(handle);
}

void SysAudio::stop(AudioHandle handle, uint16_t fadeMs)
{
    if (handle == AUDIO_HANDLE_INVALID)
        return;
    AudioCommand command;
    command.type = AudioCommandType::StopHandle;
    command.handle = handle;
    command.fadeMs = fadeMs;
    enqueueCommand(command, pdMS_TO_TICKS(10));
}

bool SysAudio::stopAndWait(const AudioHandle *handles, size_t count, uint32_t timeoutMs)
{
    if (!handles || count == 0)
        return true;
    if (xTaskGetCurrentTaskHandle() == g_audioTask)
        return false;

    const uint32_t startedAt = millis();
    for (size_t index = 0; index < count; ++index)
    {
        const AudioHandle handle = handles[index];
        if (handle == AUDIO_HANDLE_INVALID || !handleIsPlaying(handle))
            continue;

        AudioCommand command;
        command.type = AudioCommandType::StopHandle;
        command.handle = handle;
        command.fadeMs = 0;

        const uint32_t elapsed = millis() - startedAt;
        if (elapsed >= timeoutMs)
            return false;
        const TickType_t waitTicks = pdMS_TO_TICKS(timeoutMs - elapsed);
        if (!enqueueCommand(command, waitTicks > 0 ? waitTicks : 1))
            return false;
    }

    /*
     * stop命令与此前的play命令进入同一个FIFO队列；句柄只有在Voice清空后才会从
     * g_playingHandles移除。因此轮询全部句柄消失，就能证明Core 0不再解引用这些PCM。
     */
    while (millis() - startedAt < timeoutMs)
    {
        bool anyPlaying = false;
        for (size_t index = 0; index < count; ++index)
        {
            if (handles[index] != AUDIO_HANDLE_INVALID && handleIsPlaying(handles[index]))
            {
                anyPlaying = true;
                break;
            }
        }
        if (!anyPlaying)
            return true;
        delay(1);
    }
    return false;
}

void SysAudio::setGain(AudioHandle handle, float gain, uint16_t fadeMs)
{
    if (handle == AUDIO_HANDLE_INVALID)
        return;
    AudioCommand command;
    command.type = AudioCommandType::SetGain;
    command.handle = handle;
    command.gain = clampGain(gain);
    command.fadeMs = fadeMs;
    enqueueCommand(command, pdMS_TO_TICKS(10));
}

void SysAudio::stopBus(AudioBus bus, uint16_t fadeMs)
{
    if (!validBus(bus))
        return;
    AudioCommand command;
    command.type = AudioCommandType::StopBus;
    command.bus = bus;
    command.fadeMs = fadeMs;
    enqueueCommand(command, pdMS_TO_TICKS(10));
}

void SysAudio::setBusGain(AudioBus bus, float gain, uint16_t fadeMs)
{
    if (!validBus(bus))
        return;
    AudioCommand command;
    command.type = AudioCommandType::SetBusGain;
    command.bus = bus;
    command.gain = clampGain(gain);
    command.fadeMs = fadeMs;
    enqueueCommand(command, pdMS_TO_TICKS(10));
}

void SysAudio::playTone(uint16_t freq, uint16_t duration_ms, uint16_t delayMs)
{
    if (freq == 0 || duration_ms == 0 || sysConfig.volume == 0)
        return;
    AudioCommand command;
    command.type = AudioCommandType::PlayTone;
    command.frequency = freq;
    command.durationMs = duration_ms;
    command.delayMs = delayMs;
    enqueueCommand(command);
}

void SysAudio::playGlitch()
{
    if (sysConfig.volume == 0)
        return;
    AudioCommand command;
    command.type = AudioCommandType::PlayGlitch;
    command.durationMs = (uint16_t)random(3, 6);
    command.startFrequency = (float)random(3500, 4500);
    command.endFrequency = 800.0f;
    enqueueCommand(command);
}

void SysAudio::playWAV(const uint8_t *data, uint32_t len, bool loop)
{
    if (!data || len < 4)
    {
        Serial.println("[音频] 兼容 WAV 播放失败：PCM 数据为空或长度过短。");
        return;
    }

    uint32_t alignedLen = len & ~0x01UL;
    AudioClip clip;
    clip.samples = reinterpret_cast<const int16_t *>(data);
    clip.frameCount = alignedLen / sizeof(int16_t);
    clip.sampleRate = AUDIO_SAMPLE_RATE;
    clip.loopEndFrame = clip.frameCount;

    AudioPlayOptions options;
    options.bus = loop ? AudioBus::Ambient : AudioBus::Effect;
    options.loopMode = loop ? AudioLoopMode::Exact : AudioLoopMode::None;

    stopWAV();
    g_legacyHandle = play(clip, options);
}

void SysAudio::stopWAV()
{
    if (g_legacyHandle != AUDIO_HANDLE_INVALID)
    {
        stop(g_legacyHandle);
        g_legacyHandle = AUDIO_HANDLE_INVALID;
    }
}

void SysAudio_Sleep()
{
    if (g_audioTask != nullptr && g_audioEvents != nullptr)
    {
        xEventGroupClearBits(g_audioEvents, AUDIO_EVENT_SUSPENDED);
        AudioCommand command;
        command.type = AudioCommandType::Suspend;
        if (enqueueCommand(command, pdMS_TO_TICKS(100)))
        {
            EventBits_t bits = xEventGroupWaitBits(
                g_audioEvents,
                AUDIO_EVENT_SUSPENDED,
                pdFALSE,
                pdTRUE,
                pdMS_TO_TICKS(200));
            if ((bits & AUDIO_EVENT_SUSPENDED) == 0)
                Serial.println("[音频] 休眠前等待后台任务暂停超时。");
        }
    }
    BSP::AudioI2S::Sleep();
}

void SysAudio_Wakeup()
{
    BSP::AudioI2S::Wakeup();
    AudioCommand command;
    command.type = AudioCommandType::Resume;
    if (!enqueueCommand(command, pdMS_TO_TICKS(100)))
        Serial.println("[音频] 唤醒命令投递失败。");
}
