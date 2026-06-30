/*
【模块职责】I2S0 音频输出板级驱动实现。这里直接接触 ESP-IDF I2S API，供 sys_audio 后台任务调用。
*/
#include "bsp/bsp_audio_i2s.h"
#include "bsp/bsp_pins.h"
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
    // I2S 写入会被音频后台任务调用，互斥锁用于避免休眠/清空 DMA 时与写入交叉。
    SemaphoreHandle_t s_i2sMutex = NULL;

    // 记录 I2S 驱动是否已成功安装并绑定引脚。
    bool s_ready = false;
}

namespace BSP::AudioI2S
{
    // 【函数说明】安装 I2S0 TX 驱动，并配置 BCLK/LRCK/DOUT 引脚。
    bool Begin(uint32_t sampleRate)
    {
        if (s_i2sMutex == NULL)
            s_i2sMutex = xSemaphoreCreateMutex();

        // 当前音频链路固定使用 16bit stereo，sys_audio 会按这个格式生成采样。
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate = sampleRate,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 6,
            .dma_buf_len = 512,
            .use_apll = false,
            .tx_desc_auto_clear = true};

        i2s_pin_config_t pin_config = {
            .bck_io_num = Pins::I2S_BCLK,
            .ws_io_num = Pins::I2S_LRC,
            .data_out_num = Pins::I2S_DOUT,
            .data_in_num = I2S_PIN_NO_CHANGE};

        esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            Serial.printf("[BSP][音频] I2S 驱动安装失败：%d。\n", (int)err);
            s_ready = false;
            return false;
        }

        // I2S 驱动安装后再绑定板级引脚，避免上层散落具体 GPIO。
        err = i2s_set_pin(I2S_NUM_0, &pin_config);
        if (err != ESP_OK)
        {
            Serial.printf("[BSP][音频] I2S 引脚配置失败：%d。\n", (int)err);
            s_ready = false;
            return false;
        }

        // 清空 DMA，避免上电或重启驱动后播放到旧缓冲内容。
        i2s_zero_dma_buffer(I2S_NUM_0);
        s_ready = true;
        Serial.println("[BSP][音频] I2S0 输出已初始化。");
        return true;
    }

    // 【函数说明】返回 I2S 硬件输出是否可用。
    bool IsReady()
    {
        return s_ready;
    }

    // 【函数说明】向 I2S 写入一批 16bit 采样。
    bool Write(const int16_t *samples, size_t sampleCount, TickType_t ticksToWait)
    {
        if (!s_ready || !samples || sampleCount == 0 || s_i2sMutex == NULL)
            return false;

        // 保护 i2s_write，避免与休眠清 DMA 同时访问驱动。
        size_t bytesWritten = 0;
        if (xSemaphoreTake(s_i2sMutex, ticksToWait) != pdTRUE)
            return false;
        esp_err_t err = i2s_write(I2S_NUM_0, samples, sampleCount * sizeof(int16_t), &bytesWritten, ticksToWait);
        xSemaphoreGive(s_i2sMutex);
        return err == ESP_OK && bytesWritten > 0;
    }

    // 【函数说明】清空 I2S DMA 缓冲。
    void ClearDma()
    {
        if (s_ready)
            i2s_zero_dma_buffer(I2S_NUM_0);
    }

    // 【函数说明】休眠前停止 I2S 输出并清空缓冲。
    void Sleep()
    {
        if (!s_ready)
            return;

        // 先清空再停止，减少功放端残留短音。
        i2s_zero_dma_buffer(I2S_NUM_0);
        i2s_stop(I2S_NUM_0);
    }

    // 【函数说明】唤醒后恢复 I2S 输出。
    void Wakeup()
    {
        if (s_ready)
            i2s_start(I2S_NUM_0);
    }
}

