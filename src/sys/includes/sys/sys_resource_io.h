/*
【模块职责】为 FATFS 应用资源提供统一的只读打开、WAV 校验加载和 RGB565 校验加载能力。
【调用关系】各资源域在启动预热或语言切换时调用；本模块只依赖 HAL 的 FAT 所有权状态和 SysAudio 的 AudioClip 描述。
【重要约束】本模块不解析任何业务 JSON、不注册音频、不决定绘制或播放；成功返回的 PSRAM 内存由输出资源对象独占持有。
*/
#pragma once

#include <Arduino.h>
#include <FS.h>
#include "sys/sys_audio.h"

/** FATFS只读资源路径；当前所有应用资源都必须位于 /Resources 之下，不提供旧目录回退。 */
struct SysResourcePath
{
    const char *path;

    // 显式构造函数兼容当前GCC 8.4/C++11；带默认成员初始化的结构体不能稳定使用聚合花括号传参。
    SysResourcePath(const char *resourcePath = nullptr) : path(resourcePath)
    {
    }
};

/**
 * 已从 WAV data 段加载到 PSRAM 的常驻音频。
 * pcm 必须在 SysAudio 的异步播放实例全部结束前保持有效；当前资源策略是加载后常驻到重启。
 */
struct SysLoadedAudioAsset
{
    uint8_t *pcm;
    uint32_t pcmBytes;
    AudioClip clip;

    SysLoadedAudioAsset() : pcm(nullptr), pcmBytes(0), clip() {}
    ~SysLoadedAudioAsset() { reset(); }
    SysLoadedAudioAsset(const SysLoadedAudioAsset &) = delete;
    SysLoadedAudioAsset &operator=(const SysLoadedAudioAsset &) = delete;

    bool valid() const { return pcm != nullptr && pcmBytes > 0 && clip.samples != nullptr; }
    void reset();
};

/**
 * 不转移所有权的RGB565只读视图。
 * pixels的生命周期由资源域持有者保证；UI只能在对应资源仍常驻时同步读取，不能释放或修改。
 */
struct SysRgb565View
{
    const uint16_t *pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;

    bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
};

/** 已加载到 PSRAM 的行优先 RGB565 图片；pixels 由本对象独占持有。 */
struct SysLoadedRgb565Asset
{
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;

    SysLoadedRgb565Asset() : pixels(nullptr), width(0), height(0) {}
    ~SysLoadedRgb565Asset() { reset(); }
    SysLoadedRgb565Asset(const SysLoadedRgb565Asset &) = delete;
    SysLoadedRgb565Asset &operator=(const SysLoadedRgb565Asset &) = delete;

    bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
    SysRgb565View view() const
    {
        SysRgb565View result;
        result.pixels = pixels;
        result.width = width;
        result.height = height;
        return result;
    }
    void reset();
};

namespace SysResourceIO
{
    /**
     * 【接口说明】打开 /Resources 目录下的一份 FATFS 文件。
     * 【参数】label 只用于中文日志；resolvedPath 可选，成功时返回实际命中的运行时路径。
     * 【返回值】FAT 未由 ESP 挂载、路径无效或当前文件不存在时返回 false。
     * 【线程约束】只能在启动流程或 Arduino 主循环调用，不能从任务回调或中断调用。
     */
    bool OpenRead(const SysResourcePath &resourcePath,
                  fs::File &outFile,
                  const char *label,
                  String *resolvedPath = nullptr);

    /**
     * 【接口说明】扫描 RIFF/WAV 的 fmt 与 data 段，把支持的纯 PCM 数据加载到 PSRAM。
     * 【格式约束】只接受PCM、44100Hz、16bit、单声道；trimLoopSilence仅供明确的循环环境音使用。
     * 【所有权】成功后 out 独占PCM；若 out 已有效则直接复用，避免重复初始化造成泄漏。
     */
    bool LoadWav(const SysResourcePath &resourcePath,
                 const char *label,
                 bool trimLoopSilence,
                 SysLoadedAudioAsset &out);

    /**
     * 【接口说明】按调用方给出的固定宽高加载无文件头、行优先排列的RGB565图片。
     * 【尺寸约束】文件字节数必须严格等于width*height*2；尺寸不符时拒绝加载，不能裁切或猜测。
     * 【所有权】成功后out独占PSRAM像素；若out已经有效则直接复用，适合启动阶段统一预热。
     * 【调用时机】只允许在FATFS归ESP持有时从启动流程或Arduino主线程调用，UI层只读取返回视图。
     */
    bool LoadRgb565(const SysResourcePath &resourcePath,
                    const char *label,
                    uint16_t width,
                    uint16_t height,
                    SysLoadedRgb565Asset &out);

    /**
     * 【接口说明】加载无文件头的正方形 RGB565 图片，并由字节数推断边长。
     * 【尺寸约束】推断边长必须在 minSide~maxSide 之间，否则拒绝资源。
     * 【所有权】成功后 out 独占像素；若 out 已有效则直接复用。
     */
    bool LoadSquareRgb565(const SysResourcePath &resourcePath,
                          const char *label,
                          uint16_t minSide,
                          uint16_t maxSide,
                          SysLoadedRgb565Asset &out);
}
