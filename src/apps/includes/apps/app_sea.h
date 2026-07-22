/*
【模块职责】提供 Sea 应用的可选环境音绑定接口。
【默认行为】当前固件不注册任何回调，因此雨声和雷声均不会播放；未来音频模块可以在启动阶段
注册回调，不需要让 AppSea 直接依赖 SysAudio 或把音频策略写进 UIFluidSurface。
*/
#pragma once

#include <Arduino.h>

struct AppSeaAudioBinding
{
    /** 雨天状态改变时调用；active=false 也用于 Sea 离开前台时停止未来的环境声。 */
    void (*onRainStateChanged)(bool active) = nullptr;

    /** 雷击视觉事件发生时调用；delay_ms 是建议的雷声延迟，当前实现只保留接口不播放。 */
    void (*onThunder)(uint8_t intensity_percent, uint16_t delay_ms) = nullptr;

    /**
     * 当前叙事句子变化时调用。audio_bind 来自 sea_dialogues.json 当前句的 audio 字段；
     * nullptr 或空字符串表示停止上一句音频。指针只在回调期间有效，接收方必须立即复制或解析。
     */
    void (*onNarrativeLineChanged)(const char *audio_bind) = nullptr;
};

/** 注册可选环境音回调；传入 nullptr 会清除绑定。只能在主循环初始化阶段调用。 */
void AppSea_SetAudioBinding(const AppSeaAudioBinding *binding);

/** 清除所有环境音回调，供音频服务退出或切换资源时调用。 */
void AppSea_ClearAudioBinding();
