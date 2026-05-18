// 文件：src/sys/sys_res.cpp
// 职责：把 LittleFS 中的常驻资源加载到 PSRAM，包括 WAV 音频、硬币贴图和抽卡身份池。
// 说明：WAV 不再假设 44 字节固定头，而是扫描 RIFF chunk，找到真正的 data 段后再缓存 PCM 数据。
#include "sys_res.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <new>

uint8_t *g_wav_procedure = nullptr;
uint32_t g_wav_procedure_len = 0;
uint8_t *g_wav_final = nullptr;
uint32_t g_wav_final_len = 0;

uint8_t *g_wav_heads = nullptr;
uint32_t g_wav_heads_len = 0;
uint8_t *g_wav_tails = nullptr;
uint32_t g_wav_tails_len = 0;

uint8_t *g_ahab_sound = nullptr;
uint32_t g_ahab_sound_len = 0;

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

/**
 * 从 WAV 文件中定位真正的 PCM data chunk。
 *
 * 旧代码固定 f.seek(44)，但当前资源文件里 WAV 头包含 LIST/INFO 等附加 chunk，
 * 真正的 data 段不在 offset 44，而是在更靠后的位置。
 *
 * 本函数按 RIFF 结构逐个扫描 chunk：
 * RIFF header -> fmt/LIST/... -> data
 * 找到 data 后返回 data_offset 和 data_len。
 */
static bool _FindWavDataChunk(File &file, uint32_t *out_offset, uint32_t *out_len)
{
    if (!out_offset || !out_len)
        return false;

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

        if (_ChunkIdEquals(chunk_header, "data"))
        {
            if (data_offset >= file_size)
                return false;

            uint32_t readable_len = chunk_size;
            if (data_offset + readable_len > file_size)
            {
                readable_len = file_size - data_offset;
            }

            *out_offset = data_offset;
            *out_len = readable_len;
            return true;
        }

        /*
         * RIFF chunk 以偶数字节对齐。
         * 如果 chunk_size 是奇数，下一个 chunk 前会有 1 字节 padding。
         */
        offset = data_offset + chunk_size + (chunk_size & 0x01);
    }

    return false;
}

/**
 * 把 WAV 的 PCM data 段加载进 PSRAM。
 *
 * 输出给 SysAudio 的数据必须是“纯 PCM”，不能包含 RIFF/LIST/data 头。
 * 同时当前音频输出按 44.1kHz / 16bit / stereo 处理，一帧是 4 字节，
 * 因此缓存长度会向下对齐到 4 字节，避免最后半个声道帧进入 I2S。
 */
static bool _LoadWavPcmToPsram(const char *path, uint8_t **out_data, uint32_t *out_len)
{
    if (!path || !out_data || !out_len)
        return false;

    *out_data = nullptr;
    *out_len = 0;

    File file = LittleFS.open(path, "r");
    if (!file)
    {
        Serial.printf("[资源管家] WAV 加载失败：未找到 %s\n", path);
        return false;
    }

    uint32_t data_offset = 0;
    uint32_t data_len = 0;
    if (!_FindWavDataChunk(file, &data_offset, &data_len))
    {
        Serial.printf("[资源管家] WAV 加载失败：%s 未找到有效 data 段。\n", path);
        file.close();
        return false;
    }

    uint32_t aligned_len = data_len & ~0x03UL;
    if (aligned_len < 4)
    {
        Serial.printf("[资源管家] WAV 加载失败：%s data 段过短，len=%lu。\n", path, (unsigned long)data_len);
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

    file.seek(data_offset);
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

    Serial.printf(
        "[资源管家] WAV 已缓存：%s，data_offset=%lu，pcm_len=%lu。\n",
        path,
        (unsigned long)data_offset,
        (unsigned long)aligned_len
    );
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

    // 1. 加载 WAV 音频。这里会解析 RIFF chunk，只缓存真正的 PCM data 段。
    _LoadWavPcmToPsram("/assets/procedure.wav", &g_wav_procedure, &g_wav_procedure_len);
    _LoadWavPcmToPsram("/assets/final.wav", &g_wav_final, &g_wav_final_len);
    _LoadWavPcmToPsram("/assets/coins/heads.wav", &g_wav_heads, &g_wav_heads_len);
    _LoadWavPcmToPsram("/assets/coins/tails.wav", &g_wav_tails, &g_wav_tails_len);
    _LoadWavPcmToPsram("/assets/Ahab.wav", &g_ahab_sound, &g_ahab_sound_len);

    // 2. 加载 3 套硬币贴图：普通、红色、绿色。
    //    兼容旧 64×64 bin，也支持后续替换为 96×96 bin；缺失时回退到普通金色贴图。
    String prefixes[3] = {"/assets/coins/", "/assets/coins/r", "/assets/coins/g"};
    for (int i = 0; i < 3; i++)
    {
        String heads_path = prefixes[i] + "heads.bin";
        if (!_LoadCoinBitmapToPsram(heads_path.c_str(), &g_img_heads[i], &g_img_heads_size[i]))
        {
            _LoadCoinBitmapToPsram("/assets/coins/heads.bin", &g_img_heads[i], &g_img_heads_size[i]);
        }

        String tails_path = prefixes[i] + "tails.bin";
        if (!_LoadCoinBitmapToPsram(tails_path.c_str(), &g_img_tails[i], &g_img_tails_size[i]))
        {
            _LoadCoinBitmapToPsram("/assets/coins/tails.bin", &g_img_tails[i], &g_img_tails_size[i]);
        }
    }

    // 3. 加载纺织机答案池 JSON。sys_oracle 只解析这两段已挂载素材，不直接反复读取 LittleFS。
    _LoadTextToPsram("/assets/oracle_zh.json", &g_oracle_json_zh, &g_oracle_json_zh_len);
    _LoadTextToPsram("/assets/oracle_en.json", &g_oracle_json_en, &g_oracle_json_en_len);

    // 4. 加载抽卡身份池，并按星级建立索引表，供提取部模拟快速随机抽取。
    File f_json = LittleFS.open("/assets/ids.json", "r");
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
            Serial.println("[资源管家] ids.json 解析失败或为空！");
        }
        f_json.close();
    }
    else
    {
        Serial.println("[资源管家] 未找到 /assets/ids.json！");
    }

    Serial.println("[资源管家] 常驻资产挂载完毕！");
}
