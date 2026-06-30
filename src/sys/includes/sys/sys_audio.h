/*
【模块职责】I2S 音频接口。提供异步短音效、故障音和 PSRAM WAV 播放；宏 SYS_SOUND_* 被转接到 sys_feedback，减少 App 直接依赖音频细节。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_audio.h
#pragma once
#include <Arduino.h>
#include "sys/sys_haptic.h"
class SysAudio
{
public:
    // 【函数说明】初始化 I2S0 和后台音频任务；必须在播放 tone/WAV 前调用。
    void begin(); // 初始化 I2S 硬件与 Core 0 音频线程

    // 【接口说明】异步短音效接口：调用后立即返回，采样生成和 I2S 写入由后台音频任务完成。
    void playTone(uint16_t freq, uint16_t duration_ms);
    void playGlitch();

    // 【接口说明】让后台任务播放一段已经加载在内存中的 PCM/WAV 数据，可选择循环。
    void playWAV(const uint8_t *data, uint32_t len, bool loop = false);
    void stopWAV();
    void SysAudio_Sleep();
    void SysAudio_Wakeup();
};

extern SysAudio sysAudio;

// 【接口说明】停止 I2S 时钟并清空 DMA，供 HAL 休眠前调用。
void SysAudio_Sleep();
void SysAudio_Wakeup();

#include "sys/sys_feedback.h"

#define SYS_SOUND_CONFIRM() Feedback_PlayConfirm()
#define SYS_SOUND_ERROR() Feedback_PlayError()
#define SYS_SOUND_NAV() Feedback_PlayKnobTick()
#define SYS_SOUND_LONG() Feedback_PlayBack()
#define SYS_SOUND_GLITCH() Feedback_PlayGlitch()
#define SYS_SOUND_SUCCESS_4BEEPS() Feedback_PlayDecodeComplete()
