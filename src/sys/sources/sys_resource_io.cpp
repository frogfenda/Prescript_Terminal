/*
【模块职责】实现 FATFS 通用只读资源加载，统一处理挂载检查、/Resources 路径、PSRAM分配和格式错误。
【实现边界】这里只理解文件格式，不理解 Prescript、Sea、Coin、Gacha 等业务含义。
*/
#include "sys/sys_resource_io.h"

#include "hal/hal_fat_storage.h"
#include <FFat.h>
#include <math.h>
#include <string.h>

namespace
{
    /**
     * 分块把已打开文件的指定字节完整读入目标缓冲区。
     *
     * 启动预热会连续读取多份常驻WAV和全屏RGB565图片。如果把整份文件交给一次
     * File::read，底层FAT/Flash读取可能长时间占住Arduino主任务，使同核Idle任务
     * 得不到运行机会并触发任务看门狗。这里把单次读取限制为16KiB，并在每块后
     * 主动阻塞一个系统Tick；这既允许Idle任务和系统服务运行，也不会改变调用方的
     * 文件位置、PSRAM所有权或“必须完整读取”的失败语义。
     *
     * @return 实际累计读取的字节数；小于bytes表示到达文件尾或底层读取失败。
     */
    size_t ReadFullyCooperatively(fs::File &file, uint8_t *buffer, size_t bytes)
    {
        constexpr size_t READ_CHUNK_BYTES = 16U * 1024U;
        size_t totalRead = 0;

        while (totalRead < bytes)
        {
            const size_t remaining = bytes - totalRead;
            const size_t wanted = min(remaining, READ_CHUNK_BYTES);
            const size_t readBytes = file.read(buffer + totalRead, wanted);
            if (readBytes == 0)
                break;

            totalRead += readBytes;

            // delay(1)会让当前loopTask真正进入阻塞态，比只调用yield更可靠地给Idle任务喂看门狗。
            delay(1);
        }
        return totalRead;
    }

    uint32_t ReadU32LE(const uint8_t *p)
    {
        return ((uint32_t)p[0]) |
               ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }

    uint16_t ReadU16LE(const uint8_t *p)
    {
        return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
    }

    bool ChunkIdEquals(const uint8_t *id, const char *tag)
    {
        return id[0] == (uint8_t)tag[0] && id[1] == (uint8_t)tag[1] &&
               id[2] == (uint8_t)tag[2] && id[3] == (uint8_t)tag[3];
    }

    struct WavPcmInfo
    {
        uint16_t format = 0;
        uint16_t channels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        uint32_t dataOffset = 0;
        uint32_t dataBytes = 0;
        bool hasFmt = false;
        bool hasData = false;
    };

    /** 按RIFF偶数字节对齐规则扫描chunk，不能假设WAV头固定为44字节。 */
    bool FindWavPcmInfo(fs::File &file, WavPcmInfo &out)
    {
        out = WavPcmInfo{};
        const uint32_t fileSize = file.size();
        if (fileSize < 12)
            return false;

        uint8_t header[12];
        file.seek(0);
        if (file.read(header, sizeof(header)) != sizeof(header) ||
            !ChunkIdEquals(header, "RIFF") || !ChunkIdEquals(header + 8, "WAVE"))
            return false;

        uint32_t offset = 12;
        while (offset + 8U <= fileSize)
        {
            uint8_t chunkHeader[8];
            file.seek(offset);
            if (file.read(chunkHeader, sizeof(chunkHeader)) != sizeof(chunkHeader))
                return false;

            const uint32_t chunkBytes = ReadU32LE(chunkHeader + 4);
            const uint32_t dataOffset = offset + 8U;
            if (ChunkIdEquals(chunkHeader, "fmt "))
            {
                if (chunkBytes < 16 || dataOffset + 16U > fileSize)
                    return false;
                uint8_t fmt[16];
                file.seek(dataOffset);
                if (file.read(fmt, sizeof(fmt)) != sizeof(fmt))
                    return false;
                out.format = ReadU16LE(fmt);
                out.channels = ReadU16LE(fmt + 2);
                out.sampleRate = ReadU32LE(fmt + 4);
                out.bitsPerSample = ReadU16LE(fmt + 14);
                out.hasFmt = true;
            }
            else if (ChunkIdEquals(chunkHeader, "data"))
            {
                if (dataOffset >= fileSize)
                    return false;
                out.dataOffset = dataOffset;
                out.dataBytes = min(chunkBytes, fileSize - dataOffset);
                out.hasData = out.dataBytes > 0;
            }

            const uint64_t next = (uint64_t)dataOffset + chunkBytes + (chunkBytes & 1U);
            if (next <= offset || next > fileSize)
                break;
            offset = (uint32_t)next;
        }
        return out.hasFmt && out.hasData;
    }

    /** 只调整循环元数据，不裁掉PCM，从而保留资源所有权和样本地址稳定性。 */
    void TrimAmbientLoopSilence(AudioClip &clip)
    {
        if (!clip.samples || clip.frameCount < 4096 || (clip.channels != 1 && clip.channels != 2))
            return;

        constexpr int16_t ACTIVE_THRESHOLD = 96;
        uint32_t firstActive = 0;
        uint32_t lastActive = clip.frameCount;
        auto frameIsActive = [&clip](uint32_t frame) -> bool
        {
            const uint32_t sampleOffset = frame * clip.channels;
            for (uint8_t channel = 0; channel < clip.channels; ++channel)
            {
                int32_t value = clip.samples[sampleOffset + channel];
                if (value < 0)
                    value = -value;
                if (value >= ACTIVE_THRESHOLD)
                    return true;
            }
            return false;
        };

        while (firstActive < clip.frameCount && !frameIsActive(firstActive))
            ++firstActive;
        while (lastActive > firstActive && !frameIsActive(lastActive - 1U))
            --lastActive;

        if (lastActive > firstActive && lastActive - firstActive >= 44100U / 10U)
        {
            clip.loopStartFrame = firstActive;
            clip.loopEndFrame = lastActive;
        }
    }

    /**
     * 已打开RGB565文件的唯一读入实现。正方形推断和固定矩形加载都汇合到这里，
     * 从而保证尺寸校验、PSRAM所有权、短读错误和中文日志完全一致。
     */
    bool LoadOpenedRgb565(fs::File &file,
                          const String &resolvedPath,
                          uint16_t width,
                          uint16_t height,
                          SysLoadedRgb565Asset &out)
    {
        if (width == 0 || height == 0)
        {
            file.close();
            return false;
        }

        const size_t expectedBytes = (size_t)width * height * sizeof(uint16_t);
        const size_t actualBytes = file.size();
        if (actualBytes != expectedBytes)
        {
            Serial.printf("[资源IO] RGB565尺寸不匹配：%s，期望=%ux%u/%u字节，实际=%u字节。\n",
                          resolvedPath.c_str(), (unsigned)width, (unsigned)height,
                          (unsigned)expectedBytes, (unsigned)actualBytes);
            file.close();
            return false;
        }

        uint16_t *buffer = (uint16_t *)ps_malloc(expectedBytes);
        if (!buffer)
        {
            Serial.printf("[资源IO] RGB565申请PSRAM失败：%s，字节=%u。\n",
                          resolvedPath.c_str(), (unsigned)expectedBytes);
            file.close();
            return false;
        }

        const size_t readBytes = ReadFullyCooperatively(file, (uint8_t *)buffer, expectedBytes);
        file.close();
        if (readBytes != expectedBytes)
        {
            Serial.printf("[资源IO] RGB565读取不完整：%s，期望=%u，实际=%u。\n",
                          resolvedPath.c_str(), (unsigned)expectedBytes, (unsigned)readBytes);
            free(buffer);
            return false;
        }

        out.pixels = buffer;
        out.width = width;
        out.height = height;
        Serial.printf("[资源IO] RGB565已预加载：%s，尺寸=%ux%u。\n",
                      resolvedPath.c_str(), (unsigned)width, (unsigned)height);
        return true;
    }
}

void SysLoadedAudioAsset::reset()
{
    free(pcm);
    pcm = nullptr;
    pcmBytes = 0;
    clip = AudioClip{};
}

void SysLoadedRgb565Asset::reset()
{
    free(pixels);
    pixels = nullptr;
    width = 0;
    height = 0;
}

bool SysResourceIO::OpenRead(const SysResourcePath &resourcePath,
                             fs::File &outFile,
                             const char *label,
                             String *resolvedPath)
{
    outFile = fs::File();
    if (resolvedPath)
        *resolvedPath = "";

    const char *safeLabel = (label && label[0] != '\0') ? label : "未命名资源";
    if (!HAL::FatStorage::IsMountedForEsp())
    {
        Serial.printf("[资源IO] FATFS未由ESP挂载，无法读取%s。\n", safeLabel);
        return false;
    }

    if (!resourcePath.path || resourcePath.path[0] == '\0')
        return false;

    fs::File candidate = FFat.open(resourcePath.path, FILE_READ);
    if (!candidate || candidate.isDirectory())
    {
        if (candidate)
            candidate.close();
        Serial.printf("[资源IO] 找不到%s：%s。\n", safeLabel, resourcePath.path);
        return false;
    }

    outFile = candidate;
    if (resolvedPath)
        *resolvedPath = resourcePath.path;
    return true;
}

bool SysResourceIO::LoadWav(const SysResourcePath &resourcePath,
                            const char *label,
                            bool trimLoopSilence,
                            SysLoadedAudioAsset &out)
{
    if (out.valid())
        return true;

    fs::File file;
    String resolvedPath;
    if (!OpenRead(resourcePath, file, label, &resolvedPath))
        return false;

    WavPcmInfo info;
    if (!FindWavPcmInfo(file, info))
    {
        Serial.printf("[资源IO] WAV缺少有效fmt或data段：%s。\n", resolvedPath.c_str());
        file.close();
        return false;
    }
    if (info.format != 1 || info.sampleRate != 44100 || info.bitsPerSample != 16 ||
        (info.channels != 1 && info.channels != 2))
    {
        Serial.printf("[资源IO] WAV格式不支持：%s，format=%u，rate=%lu，bits=%u，channels=%u。\n",
                      resolvedPath.c_str(), (unsigned)info.format, (unsigned long)info.sampleRate,
                      (unsigned)info.bitsPerSample, (unsigned)info.channels);
        file.close();
        return false;
    }

    const uint32_t frameBytes = (uint32_t)info.channels * sizeof(int16_t);
    const uint32_t alignedBytes = info.dataBytes - (info.dataBytes % frameBytes);
    if (alignedBytes < frameBytes)
    {
        Serial.printf("[资源IO] WAV数据段过短：%s，字节=%lu。\n",
                      resolvedPath.c_str(), (unsigned long)info.dataBytes);
        file.close();
        return false;
    }

    uint8_t *buffer = (uint8_t *)ps_malloc(alignedBytes);
    if (!buffer)
    {
        Serial.printf("[资源IO] WAV申请PSRAM失败：%s，字节=%lu。\n",
                      resolvedPath.c_str(), (unsigned long)alignedBytes);
        file.close();
        return false;
    }

    file.seek(info.dataOffset);
    const size_t readBytes = ReadFullyCooperatively(file, buffer, alignedBytes);
    file.close();
    if (readBytes != alignedBytes)
    {
        Serial.printf("[资源IO] WAV读取不完整：%s，期望=%lu，实际=%u。\n",
                      resolvedPath.c_str(), (unsigned long)alignedBytes, (unsigned)readBytes);
        free(buffer);
        return false;
    }

    out.pcm = buffer;
    out.pcmBytes = alignedBytes;
    out.clip.samples = reinterpret_cast<const int16_t *>(buffer);
    out.clip.frameCount = alignedBytes / frameBytes;
    out.clip.sampleRate = info.sampleRate;
    out.clip.channels = (uint8_t)info.channels;
    out.clip.loopStartFrame = 0;
    out.clip.loopEndFrame = out.clip.frameCount;
    if (trimLoopSilence)
        TrimAmbientLoopSilence(out.clip);

    Serial.printf("[资源IO] WAV已预加载：%s，PCM=%lu字节，声道=%u。\n",
                  resolvedPath.c_str(), (unsigned long)alignedBytes, (unsigned)info.channels);
    return true;
}

bool SysResourceIO::LoadSquareRgb565(const SysResourcePath &resourcePath,
                                     const char *label,
                                     uint16_t minSide,
                                     uint16_t maxSide,
                                     SysLoadedRgb565Asset &out)
{
    if (out.valid())
        return true;
    if (minSide == 0 || maxSide < minSide)
        return false;

    fs::File file;
    String resolvedPath;
    if (!OpenRead(resourcePath, file, label, &resolvedPath))
        return false;

    const size_t bytes = file.size();
    if (bytes == 0 || (bytes % sizeof(uint16_t)) != 0)
    {
        Serial.printf("[资源IO] RGB565字节数无效：%s，字节=%u。\n", resolvedPath.c_str(), (unsigned)bytes);
        file.close();
        return false;
    }

    const size_t pixels = bytes / sizeof(uint16_t);
    uint16_t side = 0;
    for (uint16_t candidate = minSide; candidate <= maxSide; ++candidate)
    {
        if ((size_t)candidate * candidate == pixels)
        {
            side = candidate;
            break;
        }
    }
    if (side == 0)
    {
        Serial.printf("[资源IO] RGB565不是%u~%u范围内的正方形：%s，字节=%u。\n",
                      (unsigned)minSide, (unsigned)maxSide, resolvedPath.c_str(), (unsigned)bytes);
        file.close();
        return false;
    }

    return LoadOpenedRgb565(file, resolvedPath, side, side, out);
}

bool SysResourceIO::LoadRgb565(const SysResourcePath &resourcePath,
                               const char *label,
                               uint16_t width,
                               uint16_t height,
                               SysLoadedRgb565Asset &out)
{
    if (out.valid())
        return true;
    if (width == 0 || height == 0)
        return false;

    fs::File file;
    String resolvedPath;
    if (!OpenRead(resourcePath, file, label, &resolvedPath))
        return false;
    return LoadOpenedRgb565(file, resolvedPath, width, height, out);
}
