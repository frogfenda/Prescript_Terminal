// 文件：src/sys/sys_audio.h
#pragma once
#include <Arduino.h>
#include "sys_haptic.h"
class SysAudio
{
public:
    void begin(); // 初始化 I2S 硬件与双核线程
    void playTone(uint16_t freq, uint16_t duration_ms);
    void playGlitch();

    // 全新的 WAV 播放接口，自带 loop 循环参数！
    void playWAV(const uint8_t *data, uint32_t len, bool loop = false);
    void stopWAV();
    void SysAudio_Sleep();
    void SysAudio_Wakeup();
};

extern SysAudio sysAudio;

#define SYS_SOUND_CONFIRM()          \
    do                               \
    {                                \
        sysAudio.playTone(2800, 40); \
        sysHaptic.playConfirm();     \
    } while (0)

// 2. 错误警报 (低频沉闷音 + 连续退回震感)
#define SYS_SOUND_ERROR()            \
    do                               \
    {                                \
        sysAudio.playTone(300, 150); \
        sysHaptic.playBack();        \
    } while (0)

// 3. 旋钮滚动 (极短促滴答音 + 极短促阻尼震感)
#define SYS_SOUND_NAV()              \
    do                               \
    {                                \
        sysAudio.playTone(3800, 10); \
        sysHaptic.playTick();        \
    } while (0)

// 4. 长按返回/删除 (长音 + 快速双击震感)
#define SYS_SOUND_LONG()             \
    do                               \
    {                                \
        sysAudio.playTone(1000, 60); \
        sysHaptic.playBack();        \
    } while (0)

// 5. 危险乱码警报 (系统故障音 + 刺痛警告震感)
#define SYS_SOUND_GLITCH()     \
    do                         \
    {                          \
        sysAudio.playGlitch(); \
        sysHaptic.playTick(); \
    } while (0)

// 6. 兜底的解码成功四连发 (对应找不到 wav 文件的替补方案)
#define SYS_SOUND_SUCCESS_4BEEPS()     \
    do                                 \
    {                                  \
        sysAudio.playTone(7000, 70);   \
        delay(60);                     \
        sysAudio.playTone(7000, 70);   \
        delay(60);                     \
        sysAudio.playTone(7000, 70);   \
        delay(60);                     \
        sysAudio.playTone(7000, 250);  \
        sysHaptic.playDecodeSuccess(); \
    } while (0)
