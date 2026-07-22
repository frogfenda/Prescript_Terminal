/*
【模块职责】系统音频混音与播放接口。

资源、分类和播放实例是三个互相独立的概念：
- AudioAssetId：唯一标识一份已经由 SysRes 从 LittleFS 加载的真实音频资源；
- AudioBus：标识环境音、对白、效果音或 UI 音，供整类调节和停止；
- AudioHandle：唯一标识某一次播放实例，同一资源可以同时得到多个不同句柄。

所有公开播放/控制接口只投递 FreeRTOS 命令，不直接写 I2S。PCM Voice、程序音 Voice、
循环位置和淡入淡出状态全部由 Core 0 音频任务独占，避免 App 主循环与后台任务交叉修改状态。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_haptic.h"

using AudioHandle = uint32_t;
constexpr AudioHandle AUDIO_HANDLE_INVALID = 0;

/** 系统内置音频资源的稳定 ID；真实 LittleFS 路径由 SysRes 统一绑定。 */
enum class AudioAssetId : uint8_t
{
    Invalid = 0,
    Procedure,
    Final,
    CoinHeads,
    CoinTails,
    Ahab,
    SeaRain,
    Count
};

/** 音频功能分类。一个总线可以同时容纳多个不同的播放实例。 */
enum class AudioBus : uint8_t
{
    Ambient = 0,
    Voice,
    Effect,
    Ui,
    Count
};

/**
 * 循环策略：
 * - None：播放到末尾后释放 Voice；
 * - Exact：到 loopEndFrame 后立即回到 loopStartFrame，不插入静音；
 * - Crossfade：在循环末尾把尾部与开头重叠混合，适合边界波形不连续的环境音。
 */
enum class AudioLoopMode : uint8_t
{
    None = 0,
    Exact,
    Crossfade
};

/**
 * 已解码 PCM 资源描述。
 * samples 指向由资源层长期持有的 16bit PCM；播放期间这段内存绝不能释放或移动。
 * 当前 I2S 输出固定为 44100Hz/16bit，混音器接受 mono 或 stereo，并把 mono 复制到双声道。
 */
struct AudioClip
{
    const int16_t *samples = nullptr;
    uint32_t frameCount = 0;
    uint32_t sampleRate = 44100;
    uint8_t channels = 2;
    uint32_t loopStartFrame = 0;
    uint32_t loopEndFrame = 0; // 0 表示使用 frameCount。
};

/** 每次播放的实例参数；未填写时作为普通 Effect 单次播放。 */
struct AudioPlayOptions
{
    AudioBus bus = AudioBus::Effect;
    AudioLoopMode loopMode = AudioLoopMode::None;
    float gain = 1.0f;
    uint16_t fadeInMs = 0;
    uint16_t crossfadeMs = 24;
};

class SysAudio
{
public:
    /** 初始化 I2S0、命令队列和 Core 0 音频任务；重复调用不会创建第二个任务。 */
    void begin();

    /**
     * 注册一份常驻 PCM 资源。通常只由 SysRes_Init() 在启动阶段调用。
     * binding 是 JSON 等数据文件使用的稳定字符串，例如 "heads"，不是 LittleFS 路径；
     * 最长 63 字节，注册表会复制内容，不要求调用方永久保留原字符串。
     */
    bool registerAsset(AudioAssetId id, const char *binding, const AudioClip &clip);
    bool hasAsset(AudioAssetId id) const;

    /**
     * 播放资源并返回唯一句柄。新播放默认不会停止其他声音；固定 PCM 槽已满或命令投递失败时
     * 返回 AUDIO_HANDLE_INVALID，调用方可以据此选择静默或语义反馈降级。
     */
    AudioHandle play(AudioAssetId id, const AudioPlayOptions &options = AudioPlayOptions());
    AudioHandle play(const char *binding, const AudioPlayOptions &options = AudioPlayOptions());
    AudioHandle play(const AudioClip &clip, const AudioPlayOptions &options = AudioPlayOptions());

    /** 只控制指定实例；fadeMs 为 0 时立即停止，否则在指定时间内淡出。 */
    void stop(AudioHandle handle, uint16_t fadeMs = 0);
    void setGain(AudioHandle handle, float gain, uint16_t fadeMs = 0);

    /** 按功能总线统一控制；不会影响其他总线中的播放实例。 */
    void stopBus(AudioBus bus, uint16_t fadeMs = 0);
    void setBusGain(AudioBus bus, float gain, uint16_t fadeMs = 0);

    /** 异步程序音接口。四个独立程序音槽允许短音重叠，槽满时直接丢弃新短音。 */
    /** delayMs 只延迟这个 Tone Voice 的起点，不阻塞调用线程或其他 Voice。 */
    void playTone(uint16_t freq, uint16_t duration_ms, uint16_t delayMs = 0);
    void playGlitch();

    /**
     * 旧 PCM 指针接口的兼容包装。它只替换上一次由 playWAV() 创建的兼容实例，
     * 不会停止通过新 play() 接口启动的其他 Voice；新代码应优先使用资源 ID。
     */
    void playWAV(const uint8_t *data, uint32_t len, bool loop = false);
    void stopWAV();
};

extern SysAudio sysAudio;

/** HAL 休眠链路使用的全局包装；会先暂停音频任务并确认，再操作 I2S。 */
void SysAudio_Sleep();
void SysAudio_Wakeup();

#include "sys/sys_feedback.h"

#define SYS_SOUND_CONFIRM() Feedback_PlayConfirm()
#define SYS_SOUND_ERROR() Feedback_PlayError()
#define SYS_SOUND_NAV() Feedback_PlayKnobTick()
#define SYS_SOUND_LONG() Feedback_PlayBack()
#define SYS_SOUND_GLITCH() Feedback_PlayGlitch()
#define SYS_SOUND_SUCCESS_4BEEPS() Feedback_PlayDecodeComplete()
