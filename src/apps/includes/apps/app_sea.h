/*
【模块职责】Sea 应用与声音策略之间的可选绑定接口。

AppSea 只发布“是否下雨、发生雷击、当前叙事句子 audio ID”这些业务事件，不直接读取文件、
分配音频 Voice 或操作 I2S。独立的 app_sea_audio_binding.cpp 把这些事件转换成 SysAudio 调用，
从而保持海面状态机、声音资源和底层混音器互相解耦。
*/
#pragma once

#include <Arduino.h>

struct AppSeaAudioBinding
{
    /** 雨天状态变化时调用；active=false 也用于页面进入后台或销毁时停止环境声。 */
    void (*onRainStateChanged)(bool active) = nullptr;

    /**
     * 雷击视觉事件发生时调用。intensity_percent 为 0~100，delay_ms 是建议的雷声延迟。
     * 当前 rain 绑定不注册此项，等加入 thunder 资源后可直接扩展而不修改流体 UI。
     */
    void (*onThunder)(uint8_t intensity_percent, uint16_t delay_ms) = nullptr;

    /**
     * 当前叙事句子变化时调用。audio_bind 来自 sea_dialogues.json 的 audio 字段；
     * nullptr 或空字符串表示停止上一句。指针只在回调期间有效，接收方必须立即解析或复制。
     */
    void (*onNarrativeLineChanged)(const char *audio_bind) = nullptr;
};

/**
 * 注册可选声音回调；传入 nullptr 会清除绑定。
 * 注册时会立即同步当前雨天和叙事状态，因此回调必须能够处理重复的相同状态。
 */
void AppSea_SetAudioBinding(const AppSeaAudioBinding *binding);

/** 清除回调前先发布停止状态，防止绑定拥有的循环音失去停止机会。 */
void AppSea_ClearAudioBinding();

/**
 * 安装 Sea 到 SysAudio 的默认适配器。AppSea 首次进入时调用；函数可重复调用且只安装一次。
 * 资源必须已经由 SysRes_Init() 注册，缺失时保持静音并允许下次进入重新尝试。
 */
void AppSeaAudio_EnsureInstalled();
