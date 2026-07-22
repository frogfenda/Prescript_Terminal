// 文件：src/sys/sys_res.cpp
// 职责：把 LittleFS 中的常驻资源加载到 PSRAM，包括 WAV 音频、硬币贴图和抽卡身份池。
// 说明：WAV 不再假设 44 字节固定头，而是扫描 RIFF fmt/data，并在格式验证后缓存 PCM 数据。
#include "sys/sys_res.h"
#include "sys/sys_constants.h"
#include "sys/sys_audio.h"
#include "sys/app_manager.h"
#include "lang/terminal_lang.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <new>

static uint8_t *g_wav_procedure = nullptr;
static uint32_t g_wav_procedure_len = 0;
static uint8_t *g_wav_final = nullptr;
static uint32_t g_wav_final_len = 0;

static uint8_t *g_wav_heads = nullptr;
static uint32_t g_wav_heads_len = 0;
static uint8_t *g_wav_tails = nullptr;
static uint32_t g_wav_tails_len = 0;

static uint8_t *g_ahab_sound = nullptr;
static uint32_t g_ahab_sound_len = 0;
static uint8_t *g_sea_rain_sound = nullptr;
static uint32_t g_sea_rain_sound_len = 0;

IdentityData *g_gacha_pool = nullptr;
int g_gacha_pool_total = 0;
int *g_gacha_1star = nullptr;
int g_count_1star = 0;
int *g_gacha_2star = nullptr;
int g_count_2star = 0;
int *g_gacha_3star = nullptr;
int g_count_3star = 0;

uint16_t *g_img_heads[3] = {nullptr, nullptr, nullptr};
uint16_t *g_img_tails[3] = {nullptr, nullptr, nullptr};
int g_img_heads_size[3] = {0, 0, 0};
int g_img_tails_size[3] = {0, 0, 0};

char *g_oracle_json_zh = nullptr;
uint32_t g_oracle_json_zh_len = 0;
char *g_oracle_json_en = nullptr;
uint32_t g_oracle_json_en_len = 0;

/**
 * 读取 4 字节小端整数。
 * WAV/RIFF 里的 chunk size 使用 little-endian 存储，不能直接把字节数组强转成 uint32_t，
 * 这样写可以避免不同编译器/对齐方式造成隐患。
 */
static uint32_t _ReadU32LE(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/** 读取 WAV fmt chunk 中的 2 字节小端整数。 */
static uint16_t _ReadU16LE(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

/**
 * 判断 4 字节 chunk id 是否匹配。
 * 例如 RIFF、WAVE、fmt 、data、LIST 等。
 */
static bool _ChunkIdEquals(const uint8_t *id, const char *tag)
{
    return id[0] == (uint8_t)tag[0] &&
           id[1] == (uint8_t)tag[1] &&
           id[2] == (uint8_t)tag[2] &&
           id[3] == (uint8_t)tag[3];
}

/** 扫描 WAV 后交给音频引擎的完整格式信息；不能只找到 data 就假定格式正确。 */
struct WavPcmInfo
{
    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_offset = 0;
    uint32_t data_len = 0;
    bool has_fmt = false;
    bool has_data = false;
};

/**
 * 按 RIFF 规则扫描 fmt/data chunk。
 *
 * 旧实现只寻找 data，所以单声道、48kHz、24bit 或压缩 WAV 都会被误当成
 * 44.1kHz/16bit/stereo 输出。这里同时取得 fmt 元数据，调用方验证后才允许缓存。
 */
static bool _FindWavPcmInfo(File &file, WavPcmInfo *out_info)
{
    if (!out_info)
        return false;

    *out_info = WavPcmInfo{};

    uint32_t file_size = file.size();
    if (file_size < 12)
        return false;

    uint8_t header[12];
    file.seek(0);
    if (file.read(header, sizeof(header)) != sizeof(header))
        return false;

    if (!_ChunkIdEquals(header + 0, "RIFF") || !_ChunkIdEquals(header + 8, "WAVE"))
    {
        return false;
    }

    uint32_t offset = 12;

    while (offset + 8 <= file_size)
    {
        uint8_t chunk_header[8];
        file.seek(offset);
        if (file.read(chunk_header, sizeof(chunk_header)) != sizeof(chunk_header))
            return false;

        uint32_t chunk_size = _ReadU32LE(chunk_header + 4);
        uint32_t data_offset = offset + 8;

        if (_ChunkIdEquals(chunk_header, "fmt "))
        {
            if (chunk_size < 16 || data_offset + 16U > file_size)
                return false;

            uint8_t fmt[16];
            file.seek(data_offset);
            if (file.read(fmt, sizeof(fmt)) != sizeof(fmt))
                return false;
            out_info->format = _ReadU16LE(fmt + 0);
            out_info->channels = _ReadU16LE(fmt + 2);
            out_info->sample_rate = _ReadU32LE(fmt + 4);
            out_info->bits_per_sample = _ReadU16LE(fmt + 14);
            out_info->has_fmt = true;
        }
        else if (_ChunkIdEquals(chunk_header, "data"))
        {
            if (data_offset >= file_size)
                return false;
            out_info->data_offset = data_offset;
            out_info->data_len = min(chunk_size, file_size - data_offset);
            out_info->has_data = out_info->data_len > 0;
        }

        /*
         * RIFF chunk 以偶数字节对齐。
         * 如果 chunk_size 是奇数，下一个 chunk 前会有 1 字节 padding。
         */
        uint64_t next_offset = (uint64_t)data_offset + chunk_size + (chunk_size & 0x01U);
        if (next_offset <= offset || next_offset > file_size)
            break;
        offset = (uint32_t)next_offset;
    }

    return out_info->has_fmt && out_info->has_data;
}

/**
 * 把 WAV 的 PCM data 段加载进 PSRAM。
 *
 * 输出给 SysAudio 的数据必须是“纯 PCM”，不能包含 RIFF/LIST/data 头。
 * 当前硬件输出固定为 44.1kHz / 16bit / stereo，但资源允许 mono 或 stereo：
 * mono 由混音器复制到左右声道。缓存长度按“声道数 × 2 字节”对齐，避免残缺帧进入混音器。
 */
static bool _LoadWavPcmToPsram(const char *path, uint8_t **out_data, uint32_t *out_len, AudioClip *out_clip)
{
    if (!path || !out_data || !out_len || !out_clip)
        return false;

    *out_data = nullptr;
    *out_len = 0;
    *out_clip = AudioClip{};

    File file = LittleFS.open(path, "r");
    if (!file)
    {
        Serial.printf("[资源管家] WAV 加载失败：未找到 %s\n", path);
        return false;
    }

    WavPcmInfo info;
    if (!_FindWavPcmInfo(file, &info))
    {
        Serial.printf("[资源管家] WAV 加载失败：%s 缺少有效 fmt 或 data 段。\n", path);
        file.close();
        return false;
    }

    if (info.format != 1 || info.sample_rate != 44100 || info.bits_per_sample != 16 ||
        (info.channels != 1 && info.channels != 2))
    {
        Serial.printf(
            "[资源管家] WAV 格式不支持：%s，format=%u，rate=%lu，bits=%u，channels=%u。\n",
            path,
            (unsigned)info.format,
            (unsigned long)info.sample_rate,
            (unsigned)info.bits_per_sample,
            (unsigned)info.channels);
        file.close();
        return false;
    }

    uint32_t frame_bytes = (uint32_t)info.channels * sizeof(int16_t);
    uint32_t aligned_len = info.data_len - (info.data_len % frame_bytes);
    if (aligned_len < frame_bytes)
    {
        Serial.printf("[资源管家] WAV 加载失败：%s data 段过短，len=%lu。\n", path, (unsigned long)info.data_len);
        file.close();
        return false;
    }

    uint8_t *buffer = (uint8_t *)ps_malloc(aligned_len);
    if (!buffer)
    {
        Serial.printf("[资源管家] WAV 加载失败：%s 申请 PSRAM 失败，len=%lu。\n", path, (unsigned long)aligned_len);
        file.close();
        return false;
    }

    file.seek(info.data_offset);
    size_t read_len = file.read(buffer, aligned_len);
    file.close();

    if (read_len != aligned_len)
    {
        Serial.printf(
            "[资源管家] WAV 加载失败：%s 读取不完整，期望=%lu，实际=%u。\n",
            path,
            (unsigned long)aligned_len,
            (unsigned)read_len
        );
        free(buffer);
        return false;
    }

    *out_data = buffer;
    *out_len = aligned_len;
    out_clip->samples = reinterpret_cast<const int16_t *>(buffer);
    out_clip->frameCount = aligned_len / frame_bytes;
    out_clip->sampleRate = info.sample_rate;
    out_clip->channels = (uint8_t)info.channels;
    out_clip->loopStartFrame = 0;
    out_clip->loopEndFrame = out_clip->frameCount;

    Serial.printf(
        "[资源管家] WAV 已缓存：%s，data_offset=%lu，pcm_len=%lu，channels=%u。\n",
        path,
        (unsigned long)info.data_offset,
        (unsigned long)aligned_len,
        (unsigned)info.channels
    );
    return true;
}

// 【函数说明】先按新资源路径加载，失败时尝试旧 /assets 路径，方便开发板尚未重刷 LittleFS 时继续启动。
static bool _LoadWavPcmWithFallback(const char *path,
                                    const char *legacy_path,
                                    uint8_t **out_data,
                                    uint32_t *out_len,
                                    AudioClip *out_clip)
{
    if (_LoadWavPcmToPsram(path, out_data, out_len, out_clip))
        return true;
    if (legacy_path && strcmp(path, legacy_path) != 0)
        return _LoadWavPcmToPsram(legacy_path, out_data, out_len, out_clip);
    return false;
}

/**
 * 为持续环境音剔除文件首尾的近静音区，循环边界仍保留在 AudioClip 元数据里。
 * 这里只用于明确标记为环境循环的资源；普通对白/效果音必须保留原始起止时序。
 */
static void _TrimAmbientLoopSilence(AudioClip &clip)
{
    if (!clip.samples || clip.frameCount < 4096 || (clip.channels != 1 && clip.channels != 2))
        return;

    constexpr int16_t ACTIVE_THRESHOLD = 96;
    uint32_t first_active = 0;
    uint32_t last_active = clip.frameCount;

    auto frame_is_active = [&clip](uint32_t frame) -> bool
    {
        uint32_t sample = frame * clip.channels;
        for (uint8_t channel = 0; channel < clip.channels; ++channel)
        {
            int32_t value = clip.samples[sample + channel];
            if (value < 0)
                value = -value;
            if (value >= ACTIVE_THRESHOLD)
                return true;
        }
        return false;
    };

    while (first_active < clip.frameCount && !frame_is_active(first_active))
        first_active++;
    while (last_active > first_active && !frame_is_active(last_active - 1U))
        last_active--;

    // 至少保留 100ms 可循环内容；异常或几乎全静音的素材继续使用完整边界。
    if (last_active > first_active && last_active - first_active >= 44100U / 10U)
    {
        clip.loopStartFrame = first_active;
        clip.loopEndFrame = last_active;
    }
}

/** 加载、可选修正循环点，并同时注册数字 ID 与 JSON 稳定绑定名。 */
static bool _LoadAndRegisterAudio(AudioAssetId id,
                                  const char *binding,
                                  const char *path,
                                  const char *legacy_path,
                                  uint8_t **legacy_data,
                                  uint32_t *legacy_len,
                                  bool trim_loop_silence)
{
    AudioClip clip;
    if (!_LoadWavPcmWithFallback(path, legacy_path, legacy_data, legacy_len, &clip))
        return false;
    if (trim_loop_silence)
        _TrimAmbientLoopSilence(clip);
    if (!sysAudio.registerAsset(id, binding, clip))
    {
        Serial.printf("[资源管家] WAV 注册失败：%s。\n", path);
        return false;
    }
    return true;
}


/**
 * 把普通文本/JSON 文件加载到 PSRAM，并补 0 结尾。
 *
 * 这类资源不是二进制贴图，也不需要 WAV chunk 解析；
 * 但 JSON 解析器需要稳定的内存区域，所以资源管家统一负责从 LittleFS 挂载到内存。
 */
static bool _LoadTextToPsram(const char *path, char **out_data, uint32_t *out_len)
{
    if (!path || !out_data || !out_len)
        return false;

    *out_data = nullptr;
    *out_len = 0;

    File file = LittleFS.open(path, "r");
    if (!file)
    {
        Serial.printf("[资源管家] 文本素材加载失败：未找到 %s\n", path);
        return false;
    }

    uint32_t len = file.size();
    if (len == 0)
    {
        Serial.printf("[资源管家] 文本素材加载失败：%s 是空文件。\n", path);
        file.close();
        return false;
    }

    char *buffer = (char *)ps_malloc(len + 1);
    if (!buffer)
    {
        Serial.printf("[资源管家] 文本素材加载失败：%s 申请 PSRAM 失败，len=%lu。\n", path, (unsigned long)len);
        file.close();
        return false;
    }

    size_t read_len = file.read((uint8_t *)buffer, len);
    file.close();

    if (read_len != len)
    {
        Serial.printf("[资源管家] 文本素材加载失败：%s 读取不完整，期望=%lu，实际=%u。\n", path, (unsigned long)len, (unsigned)read_len);
        free(buffer);
        return false;
    }

    buffer[len] = '\0';
    *out_data = buffer;
    *out_len = len;

    Serial.printf("[资源管家] 文本素材已挂载：%s，len=%lu。\n", path, (unsigned long)len);
    return true;
}

// 【函数说明】文本素材新路径加载失败时回退旧路径；用于运行期资源目录迁移阶段。
static bool _LoadTextWithFallback(const char *path, const char *legacy_path, char **out_data, uint32_t *out_len)
{
    if (_LoadTextToPsram(path, out_data, out_len))
        return true;
    if (legacy_path && strcmp(path, legacy_path) != 0)
        return _LoadTextToPsram(legacy_path, out_data, out_len);
    return false;
}

/**
 * 加载固定尺寸的二进制图片到 PSRAM。
 * 当前硬币素材是 64×64 RGB565，因此固定读取 8192 字节。
 */
static bool _LoadBinaryToPsram(const char *path, uint8_t *dst, uint32_t expected_len)
{
    if (!path || !dst || expected_len == 0)
        return false;

    File file = LittleFS.open(path, "r");
    if (!file)
        return false;

    size_t read_len = file.read(dst, expected_len);
    file.close();

    return read_len == expected_len;
}

/**
 * 加载 RGB565 硬币贴图。
 *
 * 旧工程使用 64×64 bin，后续可以直接替换成 96×96 bin。这里按文件长度自动判断
 * 正方形边长，并把边长记录给 ui_coin 渲染器，避免 UI 层继续写死 64。
 */
static bool _LoadCoinBitmapToPsram(const char *path, uint16_t **out, int *out_size)
{
    if (out) *out = nullptr;
    if (out_size) *out_size = 0;

    if (!path || !out || !out_size)
        return false;

    File file = LittleFS.open(path, "r");
    if (!file)
        return false;

    size_t bytes = file.size();
    if (bytes == 0 || (bytes % 2) != 0)
    {
        file.close();
        Serial.printf("[资源管家] 硬币贴图尺寸异常：%s, bytes=%u。\n", path, (unsigned)bytes);
        return false;
    }

    int side = 0;
    size_t pixels = bytes / 2;
    for (int candidate = 32; candidate <= 128; ++candidate)
    {
        if ((size_t)candidate * (size_t)candidate == pixels)
        {
            side = candidate;
            break;
        }
    }

    if (side == 0)
    {
        file.close();
        Serial.printf("[资源管家] 硬币贴图不是 32~128 范围内的正方形 RGB565：%s。\n", path);
        return false;
    }

    uint16_t *buf = (uint16_t *)ps_malloc(bytes);
    if (!buf)
    {
        file.close();
        Serial.printf("[资源管家] 硬币贴图 PSRAM 申请失败：%s, bytes=%u。\n", path, (unsigned)bytes);
        return false;
    }

    size_t read_len = file.read((uint8_t *)buf, bytes);
    file.close();
    if (read_len != bytes)
    {
        free(buf);
        Serial.printf("[资源管家] 硬币贴图读取不完整：%s。\n", path);
        return false;
    }

    *out = buf;
    *out_size = side;
    Serial.printf("[资源管家] 硬币贴图已挂载：%s, %dx%d。\n", path, side, side);
    return true;
}

/**
 * 初始化资源系统。
 *
 * 启动时把常用音频和图像一次性加载到 PSRAM，后续 App 播放/绘制时只访问内存，
 * 避免在动画或音频播放过程中反复从 LittleFS 读取导致卡顿。
 */
void SysRes_Init()
{
    Serial.println("[资源管家] 正在将高清材质与音频吸入 PSRAM 常驻...");

    /*
     * 1. 加载并注册音频资源。
     *
     * App 以后只依赖 AudioAssetId 或 JSON 稳定 binding，不再持有真实路径。
     * PCM 指针只由本资源模块长期持有，App 不再获得或传递裸指针。
     * procedure 是现有唯一持续循环素材，因此为它探测首尾近静音并写入 loopStart/loopEnd；
     * 其他效果音必须保留完整时序，不能使用这个修剪步骤。
     */
    _LoadAndRegisterAudio(AudioAssetId::Procedure, "procedure", PrescriptConst::AUDIO_PROCEDURE_WAV, "/assets/procedure.wav",
                          &g_wav_procedure, &g_wav_procedure_len, true);
    _LoadAndRegisterAudio(AudioAssetId::Final, "final", PrescriptConst::AUDIO_FINAL_WAV, "/assets/final.wav",
                          &g_wav_final, &g_wav_final_len, false);
    _LoadAndRegisterAudio(AudioAssetId::CoinHeads, "heads", "/common/coins/heads.wav", "/assets/coins/heads.wav",
                          &g_wav_heads, &g_wav_heads_len, false);
    _LoadAndRegisterAudio(AudioAssetId::CoinTails, "tails", "/common/coins/tails.wav", "/assets/coins/tails.wav",
                          &g_wav_tails, &g_wav_tails_len, false);
    _LoadAndRegisterAudio(AudioAssetId::Ahab, "Ahab", PrescriptConst::AUDIO_AHAB_WAV, "/assets/Ahab.wav",
                          &g_ahab_sound, &g_ahab_sound_len, false);
    _LoadAndRegisterAudio(AudioAssetId::SeaRain, "sea.rain", PrescriptConst::AUDIO_SEA_RAIN_WAV, nullptr,
                          &g_sea_rain_sound, &g_sea_rain_sound_len, true);

    // 2. 加载 3 套硬币贴图：普通、红色、绿色。
    //    兼容旧 64×64 bin，也支持后续替换为 96×96 bin；缺失时回退到普通金色贴图。
    String prefixes[3] = {String(PrescriptConst::COIN_ASSET_DIR), String(PrescriptConst::COIN_ASSET_DIR) + "r", String(PrescriptConst::COIN_ASSET_DIR) + "g"};
    String legacy_prefixes[3] = {String(PrescriptConst::COIN_ASSET_LEGACY_DIR), String(PrescriptConst::COIN_ASSET_LEGACY_DIR) + "r", String(PrescriptConst::COIN_ASSET_LEGACY_DIR) + "g"};
    for (int i = 0; i < 3; i++)
    {
        String heads_path = prefixes[i] + "heads.bin";
        if (!_LoadCoinBitmapToPsram(heads_path.c_str(), &g_img_heads[i], &g_img_heads_size[i]))
        {
            String legacy_heads_path = legacy_prefixes[i] + "heads.bin";
            if (!_LoadCoinBitmapToPsram(legacy_heads_path.c_str(), &g_img_heads[i], &g_img_heads_size[i]))
                _LoadCoinBitmapToPsram("/common/coins/heads.bin", &g_img_heads[i], &g_img_heads_size[i]);
            if (!g_img_heads[i])
                _LoadCoinBitmapToPsram("/assets/coins/heads.bin", &g_img_heads[i], &g_img_heads_size[i]);
        }

        String tails_path = prefixes[i] + "tails.bin";
        if (!_LoadCoinBitmapToPsram(tails_path.c_str(), &g_img_tails[i], &g_img_tails_size[i]))
        {
            String legacy_tails_path = legacy_prefixes[i] + "tails.bin";
            if (!_LoadCoinBitmapToPsram(legacy_tails_path.c_str(), &g_img_tails[i], &g_img_tails_size[i]))
                _LoadCoinBitmapToPsram("/common/coins/tails.bin", &g_img_tails[i], &g_img_tails_size[i]);
            if (!g_img_tails[i])
                _LoadCoinBitmapToPsram("/assets/coins/tails.bin", &g_img_tails[i], &g_img_tails_size[i]);
        }
    }

    // 3. 加载纺织机答案池 JSON。sys_oracle 只解析这两段已挂载素材，不直接反复读取 LittleFS。
    _LoadTextWithFallback(TerminalLang::OraclePath(LANG_ZH), TerminalLang::LegacyOraclePath(LANG_ZH), &g_oracle_json_zh, &g_oracle_json_zh_len);
    _LoadTextWithFallback(TerminalLang::OraclePath(LANG_EN), TerminalLang::LegacyOraclePath(LANG_EN), &g_oracle_json_en, &g_oracle_json_en_len);

    // 4. 加载抽卡身份池，并按星级建立索引表，供提取部模拟快速随机抽取。
    SystemLang_t ids_lang = TerminalLang::LOCKED ? TerminalLang::DEFAULT_LANG : appManager.getLanguage();
    File f_json = LittleFS.open(TerminalLang::IdsPath(ids_lang), "r");
    if (!f_json)
        f_json = LittleFS.open(TerminalLang::LegacyIdsPath(), "r");
    if (f_json)
    {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, f_json);
        if (!error && doc.size() > 0)
        {
            g_gacha_pool_total = doc.size();
            g_gacha_pool = (IdentityData *)ps_malloc(sizeof(IdentityData) * g_gacha_pool_total);
            g_gacha_1star = (int *)ps_malloc(sizeof(int) * g_gacha_pool_total);
            g_gacha_2star = (int *)ps_malloc(sizeof(int) * g_gacha_pool_total);
            g_gacha_3star = (int *)ps_malloc(sizeof(int) * g_gacha_pool_total);

            if (!g_gacha_pool || !g_gacha_1star || !g_gacha_2star || !g_gacha_3star)
            {
                Serial.println("[资源管家] 提取部数据申请 PSRAM 失败！");
            }
            else
            {
                for (int i = 0; i < g_gacha_pool_total; i++)
                {
                    new (&g_gacha_pool[i]) IdentityData();
                    g_gacha_pool[i].sinner = doc[i]["sinner"].as<String>();
                    g_gacha_pool[i].id_name = doc[i]["id"].as<String>();
                    g_gacha_pool[i].star = doc[i]["star"].as<int>();
                    g_gacha_pool[i].walp = doc[i]["walp"].as<int>();

                    if (g_gacha_pool[i].star == 1)
                        g_gacha_1star[g_count_1star++] = i;
                    else if (g_gacha_pool[i].star == 2)
                        g_gacha_2star[g_count_2star++] = i;
                    else if (g_gacha_pool[i].star == 3)
                        g_gacha_3star[g_count_3star++] = i;
                }
                Serial.printf("[资源管家] 提取部数据挂载完毕！总计载入身份: %d\n", g_gacha_pool_total);
            }
        }
        else
        {
            Serial.println("[资源管家] 当前语言 ids 文件解析失败或为空！");
        }
        f_json.close();
    }
    else
    {
        Serial.println("[资源管家] 未找到当前语言 ids 文件。");
    }

    Serial.println("[资源管家] 常驻资产挂载完毕！");
}
