// 文件：src/sys/sys_audio.cpp
// 职责：管理音频播放业务，I2S 硬件写入由 BSP 层处理，支持后台 WAV 播放和程序生成的短音效/乱码音。
#include "sys/sys_audio.h"
#include "sys/sys_config.h"
#include "hal/hal.h"
#include "bsp/bsp_audio_i2s.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>

SysAudio sysAudio;

volatile const uint8_t *g_wav_data = nullptr;
volatile uint32_t g_wav_len = 0;
volatile bool g_wav_loop = false;
volatile uint8_t g_wav_id = 0;

namespace {

static const uint32_t AUDIO_SAMPLE_RATE = 44100;
static const int AUDIO_CHUNK_FRAMES = 128;
static const int AUDIO_CHUNK_SAMPLES = AUDIO_CHUNK_FRAMES * 2; // 当前 I2S 固定 16bit stereo。
static const int AUDIO_WAV_FADE_FRAMES = 64;
static const int AUDIO_SFX_QUEUE_LEN = 8;

enum class SfxType : uint8_t
{
    Tone,
    Glitch
};

struct AudioSfxCommand
{
    SfxType type;
    uint16_t freq;
    uint16_t duration_ms;
    float start_freq;
    float end_freq;
};

struct AudioSfxState
{
    bool active = false;
    SfxType type = SfxType::Tone;
    uint32_t total_frames = 0;
    uint32_t frame_index = 0;
    uint16_t freq = 0;
    float start_freq = 0.0f;
    float end_freq = 0.0f;
    float phase = 0.0f;
    int16_t max_volume = 0;
};

QueueHandle_t g_sfx_queue = NULL;

static int16_t clampSample(int32_t v)
{
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (int16_t)v;
}

// 系统基准音量增益。
// 保留原来的平方音量曲线，只在最终输出前统一抬高基准，避免重新改动 tone/glitch 音色。
static const float AUDIO_MASTER_GAIN = 1.45f;

static float currentVolumeMultiplier()
{
    float vol_ratio = (float)sysConfig.volume / 100.0f;
    return vol_ratio * vol_ratio * AUDIO_MASTER_GAIN;
}

static void startSfx(AudioSfxState &sfx, const AudioSfxCommand &cmd)
{
    if (sysConfig.volume == 0 || cmd.duration_ms == 0)
    {
        sfx.active = false;
        return;
    }

    sfx.active = true;
    sfx.type = cmd.type;
    sfx.total_frames = (AUDIO_SAMPLE_RATE * (uint32_t)cmd.duration_ms) / 1000;
    if (sfx.total_frames == 0)
        sfx.total_frames = 1;
    sfx.frame_index = 0;
    sfx.freq = cmd.freq;
    sfx.start_freq = cmd.start_freq;
    sfx.end_freq = cmd.end_freq;
    sfx.phase = 0.0f;

    float vol_mul = currentVolumeMultiplier();
    if (cmd.type == SfxType::Glitch)
    {
        sfx.max_volume = (int16_t)(10000.0f * vol_mul);
    }
    else
    {
        int16_t max_volume = (int16_t)(12000.0f * vol_mul);
        if (cmd.freq < 1500)
            max_volume = max_volume / 2;
        sfx.max_volume = max_volume;
    }
}

static int16_t nextSfxSample(AudioSfxState &sfx)
{
    if (!sfx.active || sfx.frame_index >= sfx.total_frames || sfx.max_volume == 0)
    {
        sfx.active = false;
        return 0;
    }

    float progress = (float)sfx.frame_index / (float)sfx.total_frames;
    float sample = 0.0f;

    if (sfx.type == SfxType::Glitch)
    {
        float current_freq = sfx.start_freq - (sfx.start_freq - sfx.end_freq) * progress;
        sfx.phase += current_freq / (float)AUDIO_SAMPLE_RATE;
        while (sfx.phase > 1.0f)
            sfx.phase -= 1.0f;

        float wave = 4.0f * fabsf(sfx.phase - 0.5f) - 1.0f;
        float envelope = (1.0f - progress) * (1.0f - progress);
        sample = wave * sfx.max_volume * envelope;
    }
    else
    {
        if (sfx.freq == 0)
        {
            sfx.active = false;
            return 0;
        }

        float period = (float)AUDIO_SAMPLE_RATE / (float)sfx.freq;
        float phase = fmodf((float)sfx.frame_index, period) / period;
        float duty = (sfx.freq < 1500) ? 0.25f : 0.5f;
        float wave = (phase < duty) ? 1.0f : -1.0f;
        float linear_envelope = 1.0f - progress;
        float envelope = linear_envelope * linear_envelope;
        sample = wave * sfx.max_volume * envelope;
    }

    sfx.frame_index++;
    if (sfx.frame_index >= sfx.total_frames)
        sfx.active = false;

    return clampSample((int32_t)sample);
}

static void pollSfxQueue(AudioSfxState &sfx)
{
    if (g_sfx_queue == NULL)
        return;

    AudioSfxCommand cmd;
    // 短音效以“最后一次命令”为准，避免旋钮/乱码音在队列里堆积造成延迟。
    while (xQueueReceive(g_sfx_queue, &cmd, 0) == pdTRUE)
    {
        startSfx(sfx, cmd);
    }
}

static void enqueueSfx(const AudioSfxCommand &cmd)
{
    if (g_sfx_queue == NULL)
        return;

    if (xQueueSend(g_sfx_queue, &cmd, 0) != pdTRUE)
    {
        // 队列满时丢掉最旧的短音效，保留最新反馈，防止 UI 快速操作后声音滞后排队。
        AudioSfxCommand dropped;
        xQueueReceive(g_sfx_queue, &dropped, 0);
        xQueueSend(g_sfx_queue, &cmd, 0);
    }
}

} // namespace

/**
 * 后台音频任务。
 *
 * WAV 和 tone/glitch 都在 Core 0 的同一个任务里写 I2S。
 * App 层调用 playTone()/playGlitch() 时只投递短音效命令，不再同步生成采样、
 * 不再抢 I2S mutex，也不再为了短音效强行 stopWAV()。
 *
 * 这样 CHAOS 乱码态触发 glitch 时不会阻塞 UI 主循环，也不会打断 procedure.wav。
 */
void audio_bg_task(void *pvParameters)
{
    int16_t out[AUDIO_CHUNK_SAMPLES];
    AudioSfxState sfx;

    const uint8_t *current_data = nullptr;
    uint32_t current_len = 0;
    bool current_loop = false;
    uint8_t current_id = 0;
    uint32_t wav_sample_index = 0; // 16bit sample index；stereo 下一帧前进 2。

    while (1)
    {
        pollSfxQueue(sfx);

        const uint8_t *requested_data = (const uint8_t *)g_wav_data;
        uint32_t requested_len = g_wav_len;
        uint8_t requested_id = g_wav_id;

        if (requested_data != nullptr && requested_len >= 4 && sysConfig.volume > 0)
        {
            if (requested_data != current_data || requested_id != current_id)
            {
                current_data = requested_data;
                current_len = requested_len & ~0x03UL;
                current_loop = g_wav_loop;
                current_id = requested_id;
                wav_sample_index = 0;
            }
        }
        else
        {
            current_data = nullptr;
            current_len = 0;
            wav_sample_index = 0;
        }

        if (current_data == nullptr && !sfx.active)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int produced_samples = 0;
        float wav_mul = currentVolumeMultiplier();
        const int16_t *pcm = (const int16_t *)current_data;
        uint32_t total_wav_samples = current_len / sizeof(int16_t);

        for (int frame = 0; frame < AUDIO_CHUNK_FRAMES; frame++)
        {
            int32_t left = 0;
            int32_t right = 0;

            if (current_data != nullptr && wav_sample_index + 1 < total_wav_samples)
            {
                uint32_t wav_frame_index = wav_sample_index / 2;
                uint32_t total_wav_frames = total_wav_samples / 2;
                float env = 1.0f;

                if (current_loop && total_wav_frames > AUDIO_WAV_FADE_FRAMES)
                {
                    if (wav_frame_index < AUDIO_WAV_FADE_FRAMES)
                    {
                        env = (float)wav_frame_index / (float)AUDIO_WAV_FADE_FRAMES;
                    }
                    else if (total_wav_frames - wav_frame_index < AUDIO_WAV_FADE_FRAMES)
                    {
                        env = (float)(total_wav_frames - wav_frame_index) / (float)AUDIO_WAV_FADE_FRAMES;
                    }
                }

                left = (int32_t)(pcm[wav_sample_index] * wav_mul * env);
                right = (int32_t)(pcm[wav_sample_index + 1] * wav_mul * env);
                wav_sample_index += 2;

                if (wav_sample_index + 1 >= total_wav_samples)
                {
                    if (current_loop && g_wav_data == current_data && g_wav_id == current_id)
                    {
                        wav_sample_index = 0;
                    }
                    else
                    {
                        if (g_wav_data == current_data && g_wav_id == current_id)
                        {
                            g_wav_data = nullptr;
                            g_wav_len = 0;
                            g_wav_loop = false;
                        }
                        current_data = nullptr;
                        current_len = 0;
                        wav_sample_index = 0;
                    }
                }
            }
            else if (current_data != nullptr)
            {
                current_data = nullptr;
                current_len = 0;
                wav_sample_index = 0;
            }

            if (sfx.active)
            {
                int16_t s = nextSfxSample(sfx);
                left += s;
                right += s;
            }

            out[produced_samples++] = clampSample(left);
            out[produced_samples++] = clampSample(right);
        }

        if (produced_samples > 0)
            BSP::AudioI2S::Write(out, produced_samples, portMAX_DELAY);
    }
}

/**
 * 初始化音频输出硬件和音频后台任务。
 */
void SysAudio::begin()
{
    if (g_sfx_queue == NULL)
    {
        g_sfx_queue = xQueueCreate(AUDIO_SFX_QUEUE_LEN, sizeof(AudioSfxCommand));
    }

    BSP::AudioI2S::Begin(AUDIO_SAMPLE_RATE);

    xTaskCreatePinnedToCore(audio_bg_task, "SysAudio_Task", 4096, NULL, 1, NULL, 0);
}

/**
 * 播放一段已经缓存到 PSRAM 的 PCM WAV 数据。
 *
 * 注意：传入的 data 应该已经是 WAV data chunk 中的纯 PCM，不包含 RIFF/WAVE 头。
 * 为了兜底，入口仍会把长度向下对齐到 4 字节，因为当前输出固定按 16bit stereo 处理，
 * 一帧音频 = L 16bit + R 16bit = 4 字节。
 */
void SysAudio::playWAV(const uint8_t *data, uint32_t len, bool loop)
{
    if (!data || len < 4)
    {
        Serial.println("[音频] WAV 播放失败：数据为空或长度过短。");
        return;
    }

    uint32_t aligned_len = len & ~0x03UL;
    if (aligned_len != len)
    {
        Serial.printf(
            "[音频] WAV 长度未按 stereo frame 对齐，%lu -> %lu。\n",
            (unsigned long)len,
            (unsigned long)aligned_len
        );
    }

    if (aligned_len < 4)
        return;

    /*
     * 先递增播放 ID，让后台任务中断上一段 WAV；
     * 再写入新数据指针和长度，避免旧任务继续读已经切换的播放状态。
     */
    g_wav_id++;
    g_wav_data = nullptr;
    g_wav_loop = loop;
    g_wav_len = aligned_len;
    g_wav_data = data;
}

/**
 * 停止当前 WAV 播放。
 * 递增播放 ID 可以让后台任务即使已经缓存了旧指针，也能在下一次循环中退出。
 * 短音效队列不会被这里清空，避免 UI 操作反馈被无关的 WAV 切换吞掉。
 */
void SysAudio::stopWAV()
{
    g_wav_id++;
    g_wav_data = nullptr;
    g_wav_loop = false;
    g_wav_len = 0;
}

/**
 * 异步播放一段程序生成 tone。
 *
 * 旧实现会在调用线程同步生成采样并直接写入 I2S，CHAOS/旋钮快速触发时会卡 UI；
 * 新实现只入队命令，真正的采样生成与 I2S 写入统一交给 audio_bg_task。
 */
void SysAudio::playTone(uint16_t freq, uint16_t duration_ms)
{
    if (freq == 0 || duration_ms == 0 || sysConfig.volume == 0)
        return;

    AudioSfxCommand cmd;
    cmd.type = SfxType::Tone;
    cmd.freq = freq;
    cmd.duration_ms = duration_ms;
    cmd.start_freq = 0.0f;
    cmd.end_freq = 0.0f;
    enqueueSfx(cmd);
}

/**
 * 异步播放乱码故障音。
 *
 * 这里只生成随机参数并入队，不再 stopWAV()，因此 procedure.wav 可以继续作为底噪循环，
 * glitch 作为短促叠加音出现。
 */
void SysAudio::playGlitch()
{
    if (sysConfig.volume == 0)
        return;

    AudioSfxCommand cmd;
    cmd.type = SfxType::Glitch;
    cmd.freq = 0;
    cmd.duration_ms = random(3, 6);
    cmd.start_freq = (float)random(3500, 4500);
    cmd.end_freq = 800.0f;
    enqueueSfx(cmd);
}

void SysAudio_Sleep()
{
    if (g_sfx_queue != NULL)
        xQueueReset(g_sfx_queue);
    BSP::AudioI2S::Sleep();
}

void SysAudio_Wakeup()
{
    BSP::AudioI2S::Wakeup();
}

