/*
【模块职责】I2S0 音频输出板级驱动。负责 I2S 引脚、DMA、写入、睡眠和唤醒，混音逻辑留在 sys_audio。
*/
#pragma once
#include <Arduino.h>

namespace BSP::AudioI2S
{
    // 【函数说明】安装 I2S0 TX 驱动并绑定音频功放数据引脚。
    bool Begin(uint32_t sampleRate);

    // 【函数说明】返回 I2S 驱动是否已完成初始化，可供上层决定是否降级静音。
    bool IsReady();

    // 【函数说明】向 I2S DMA 写入 16bit stereo 采样；调用者负责准备采样内容。
    bool Write(const int16_t *samples, size_t sampleCount, TickType_t ticksToWait);

    // 【函数说明】清空 I2S DMA 缓冲，常用于停止播放或休眠前消除残留声音。
    void ClearDma();

    // 【函数说明】休眠前停止 I2S 输出，但保留驱动安装状态，方便唤醒后快速恢复。
    void Sleep();

    // 【函数说明】唤醒后重新启动 I2S 输出。
    void Wakeup();
}
