/*
【模块职责】把 Sea 应用发布的天气/叙事事件适配到系统多 Voice 音频引擎。

本文件属于 App 集成层：它可以同时依赖 AppSeaAudioBinding 和 SysAudio，但不会绘制海面，
也不会让 AppSea 或 UIFluidSurface 直接接触 LittleFS、PCM 指针和 I2S。当前只绑定 rain.wav；
雷声和逐句对白继续保留空回调，等真实资源加入后在同一适配器内扩展。
*/
#include "apps/app_sea.h"
#include "sys/sys_audio.h"

namespace
{
    bool g_binding_installed = false;
    AudioHandle g_rain_handle = AUDIO_HANDLE_INVALID;

    /**
     * 根据 Sea 前台雨天状态启动或停止唯一雨声实例。
     * 重复 active=true 不会叠加第二路雨声；停止后立即清空本地句柄，但音频任务会继续完成淡出。
     */
    void OnRainStateChanged(bool active)
    {
        if (!active)
        {
            if (g_rain_handle != AUDIO_HANDLE_INVALID)
            {
                sysAudio.stop(g_rain_handle, 700);
                g_rain_handle = AUDIO_HANDLE_INVALID;
            }
            return;
        }

        if (g_rain_handle != AUDIO_HANDLE_INVALID)
            return;

        AudioPlayOptions options;
        options.bus = AudioBus::Ambient;
        options.loopMode = AudioLoopMode::Crossfade;
        options.gain = 0.85f;
        options.fadeInMs = 700;
        options.crossfadeMs = 60;
        g_rain_handle = sysAudio.play(AudioAssetId::SeaRain, options);
    }
}

void AppSeaAudio_EnsureInstalled()
{
    if (g_binding_installed)
        return;

    AppSeaAudioBinding binding;
    binding.onRainStateChanged = OnRainStateChanged;
    AppSea_SetAudioBinding(&binding);
    g_binding_installed = true;
}
