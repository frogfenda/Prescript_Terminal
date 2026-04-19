// 文件：src/sys/sys_audio.cpp
#include "sys_audio.h"
#include "sys_config.h"
#include "hal/hal.h"
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

SysAudio sysAudio;

volatile const uint8_t *g_wav_data = nullptr;
volatile uint32_t g_wav_len = 0;
volatile bool g_wav_loop = false;
volatile uint8_t g_wav_id = 0;

SemaphoreHandle_t g_i2s_mutex = NULL;

void audio_bg_task(void *pvParameters)
{
    size_t bytes_written;
    int16_t last_val = 0;

    while (1)
    {
        if (g_wav_data != nullptr && g_wav_len > 0)
        {
            uint8_t vol = sysConfig.volume;
            if (vol == 0)
            {
                g_wav_data = nullptr;
                continue;
            }

            const uint8_t *current_data = (const uint8_t *)g_wav_data;
            uint32_t current_len = g_wav_len;
            bool is_looping = g_wav_loop;
            uint8_t current_id = g_wav_id;

            int16_t *pcm = (int16_t *)current_data;
            uint32_t total_samples = current_len / 2;

            const int CHUNK_SAMPLES = 256;
            int16_t buf[CHUNK_SAMPLES];
            uint32_t ptr = 0;

            float vol_ratio = (float)vol / 100.0f;
            float multiplier = vol_ratio * vol_ratio;
            const int FADE_LEN = 64;

            while (ptr < total_samples)
            {
                if (g_wav_data != current_data || g_wav_id != current_id)
                    break;

                int chunk = (total_samples - ptr < CHUNK_SAMPLES) ? (total_samples - ptr) : CHUNK_SAMPLES;

                for (int i = 0; i < chunk; i++)
                {
                    uint32_t pos = ptr + i;
                    float env = 1.0f;

                    if (is_looping)
                    {
                        if (pos < FADE_LEN)
                        {
                            env = (float)pos / FADE_LEN;
                        }
                        else if (total_samples - pos < FADE_LEN)
                        {
                            env = (float)(total_samples - pos) / FADE_LEN;
                        }
                    }

                    last_val = (int16_t)(pcm[pos] * multiplier * env);
                    buf[i] = last_val;
                }

                if (xSemaphoreTake(g_i2s_mutex, portMAX_DELAY))
                {
                    i2s_write(I2S_NUM_0, buf, chunk * sizeof(int16_t), &bytes_written, portMAX_DELAY);
                    xSemaphoreGive(g_i2s_mutex);
                }
                ptr += chunk;

                if (ptr >= total_samples && is_looping && g_wav_data == current_data && g_wav_id == current_id)
                {
                    ptr = 0;
                }
            }

            if (last_val != 0)
            {
                int16_t safe_buf[64];
                for (int i = 0; i < 64; i++)
                {
                    last_val = (int16_t)(last_val * 0.85f);
                    safe_buf[i] = last_val;
                }
                if (xSemaphoreTake(g_i2s_mutex, portMAX_DELAY))
                {
                    i2s_write(I2S_NUM_0, safe_buf, sizeof(safe_buf), &bytes_written, portMAX_DELAY);
                    xSemaphoreGive(g_i2s_mutex);
                }
                last_val = 0;
            }

            if (g_wav_data == current_data && g_wav_id == current_id)
            {
                g_wav_data = nullptr;
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void SysAudio::begin()
{
    if (g_i2s_mutex == NULL)
    {
        g_i2s_mutex = xSemaphoreCreateMutex();
    }

    // 【同步修改 1】：扩大 DMA 吞吐缓冲池，喂饱 44100Hz 的恐怖消耗速度
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100, // 全局升级 44100Hz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 512, // 【防卡死修改】：从 160 扩大到 512
        .use_apll = false,
        .tx_desc_auto_clear = true};
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRC,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE};
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);

    xTaskCreatePinnedToCore(audio_bg_task, "SysAudio_Task", 4096, NULL, 1, NULL, 0);
}

void SysAudio::playWAV(const uint8_t *data, uint32_t len, bool loop)
{
    if (!data || len == 0)
        return;
    g_wav_id++;
    g_wav_loop = loop;
    g_wav_len = len;
    g_wav_data = data;
}

void SysAudio::stopWAV()
{
    g_wav_id++;
    g_wav_data = nullptr;
    g_wav_loop = false;
}

void SysAudio::playTone(uint16_t freq, uint16_t duration_ms)
{
    stopWAV();
    if (freq == 0 || duration_ms == 0 || sysConfig.volume == 0)
        return;

    uint32_t sample_rate = 44100; // 同步升级
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;

    float vol_ratio = (float)sysConfig.volume / 100.0f;
    int16_t max_volume = (int16_t)(12000.0f * vol_ratio * vol_ratio);
    if (freq < 1500)
        max_volume = max_volume / 2;

    size_t bytes_written;
    const int CHUNK_SAMPLES = 256;
    int16_t buf[CHUNK_SAMPLES * 2];
    int buf_idx = 0;
    float period = (float)sample_rate / freq;

    for (uint32_t i = 0; i < total_samples; i++)
    {
        float phase = fmod((float)i, period) / period;
        float duty = (freq < 1500) ? 0.25f : 0.5f;
        float wave = (phase < duty) ? 1.0f : -1.0f;
        float linear_envelope = 1.0f - ((float)i / total_samples);
        float envelope = linear_envelope * linear_envelope;
        int16_t sample_val = (int16_t)(wave * max_volume * envelope);

        buf[buf_idx++] = sample_val;
        buf[buf_idx++] = sample_val;
        if (buf_idx >= CHUNK_SAMPLES * 2)
        {
            if (xSemaphoreTake(g_i2s_mutex, portMAX_DELAY))
            {
                i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
                xSemaphoreGive(g_i2s_mutex);
            }
            buf_idx = 0;
        }
    }
    if (buf_idx > 0)
    {
        if (xSemaphoreTake(g_i2s_mutex, portMAX_DELAY))
        {
            i2s_write(I2S_NUM_0, buf, buf_idx * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            xSemaphoreGive(g_i2s_mutex);
        }
    }
}

void SysAudio::playGlitch()
{
    stopWAV();
    if (sysConfig.volume == 0)
        return;
    uint32_t sample_rate = 44100; // 同步升级
    uint32_t duration_ms = random(3, 6);
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;

    float vol_ratio = (float)sysConfig.volume / 100.0f;
    int16_t max_volume = (int16_t)(10000.0f * vol_ratio * vol_ratio);

    size_t bytes_written;
    const int CHUNK_SAMPLES = 256;
    int16_t buf[CHUNK_SAMPLES * 2];
    int buf_idx = 0;

    float start_freq = (float)random(3500, 4500);
    float end_freq = 800.0f;
    float current_phase = 0.0f;

    for (uint32_t i = 0; i < total_samples; i++)
    {
        float progress = (float)i / total_samples;
        float current_freq = start_freq - (start_freq - end_freq) * progress;
        current_phase += current_freq / sample_rate;
        if (current_phase > 1.0f)
            current_phase -= 1.0f;
        float wave = 4.0f * fabs(current_phase - 0.5f) - 1.0f;
        float envelope = (1.0f - progress) * (1.0f - progress);
        int16_t sample_val = (int16_t)(wave * max_volume * envelope);

        buf[buf_idx++] = sample_val;
        buf[buf_idx++] = sample_val;

        // 【核心修复 2】：新增切片冲刷阀门，装满 512 个数据就送给声卡，杜绝数组越界引发重启！
        if (buf_idx >= CHUNK_SAMPLES * 2)
        {
            if (xSemaphoreTake(g_i2s_mutex, portMAX_DELAY))
            {
                i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
                xSemaphoreGive(g_i2s_mutex);
            }
            buf_idx = 0;
        }
    }
    // 把循环结束后剩下的残余数据送入声卡
    if (buf_idx > 0)
    {
        if (xSemaphoreTake(g_i2s_mutex, portMAX_DELAY))
        {
            i2s_write(I2S_NUM_0, buf, buf_idx * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            xSemaphoreGive(g_i2s_mutex);
        }
    }
}

void SysAudio_Sleep()
{
    // 清空底层缓冲并停止硬件时钟，防止唤醒错位
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_stop(I2S_NUM_0);
}

void SysAudio_Wakeup()
{
    // 唤醒后重新启动硬件
    i2s_start(I2S_NUM_0);
}
