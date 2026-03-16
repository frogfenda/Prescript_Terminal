// 文件：src/sys/sys_audio.h
#pragma once
#include <Arduino.h>

class SysAudio {
public:
    void begin(); // 初始化 I2S 硬件与双核线程
    void playTone(uint16_t freq, uint16_t duration_ms);
    void playGlitch();
    
    // 全新的 WAV 播放接口，自带 loop 循环参数！
    void playWAV(const uint8_t* data, uint32_t len, bool loop = false);
    void stopWAV();
};

extern SysAudio sysAudio;

// 将系统全局宏定义接管到独立的音频引擎
#define SYS_SOUND_CONFIRM() sysAudio.playTone(2800, 40)
#define SYS_SOUND_ERROR()   sysAudio.playTone(300, 150)
#define SYS_SOUND_NAV()     sysAudio.playTone(3800, 10)
#define SYS_SOUND_LONG()    sysAudio.playTone(1000, 60)
#define SYS_SOUND_GLITCH()  sysAudio.playGlitch()
#define SYS_SOUND_SUCCESS_4BEEPS() do { \
    sysAudio.playTone(4800, 60); delay(60); \
    sysAudio.playTone(4800, 60); delay(60); \
    sysAudio.playTone(4800, 60); delay(60); \
    sysAudio.playTone(6000, 250); \
} while(0)

