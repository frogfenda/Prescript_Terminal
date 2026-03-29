// 文件：src/sys/sys_res.cpp
#include "sys_res.h"
#include <LittleFS.h>
#include <ArduinoJson.h> // 【新增】
#include <new>           // 【新增】

uint8_t *g_wav_procedure = nullptr;
uint32_t g_wav_procedure_len = 0;
uint8_t *g_wav_final = nullptr;
uint32_t g_wav_final_len = 0;

uint8_t *g_wav_heads = nullptr;
uint32_t g_wav_heads_len = 0;
uint8_t *g_wav_tails = nullptr;
uint32_t g_wav_tails_len = 0;
// 【新增】：实体化抽卡全局池
IdentityData *g_gacha_pool = nullptr;
int g_gacha_pool_total = 0;
int *g_gacha_1star = nullptr;
int g_count_1star = 0;
int *g_gacha_2star = nullptr;
int g_count_2star = 0;
int *g_gacha_3star = nullptr;
int g_count_3star = 0;
// 【新增】：实体指针数组定义
uint16_t *g_img_heads[3] = {nullptr, nullptr, nullptr};
uint16_t *g_img_tails[3] = {nullptr, nullptr, nullptr};

void SysRes_Init()
{
    Serial.println("[资源管家] 正在将高清材质与音频吸入 PSRAM 常驻...");

    // 1. 吸入音频
    File f1 = LittleFS.open("/assets/procedure.wav", "r");
    if (f1)
    {
        g_wav_procedure_len = f1.size() - 44;
        g_wav_procedure = (uint8_t *)ps_malloc(g_wav_procedure_len);
        if (g_wav_procedure)
        {
            f1.seek(44);
            f1.read(g_wav_procedure, g_wav_procedure_len);
        }
        f1.close();
    }

    File f2 = LittleFS.open("/assets/final.wav", "r");
    if (f2)
    {
        g_wav_final_len = f2.size() - 44;
        g_wav_final = (uint8_t *)ps_malloc(g_wav_final_len);
        if (g_wav_final)
        {
            f2.seek(44);
            f2.read(g_wav_final, g_wav_final_len);
        }
        f2.close();
    }

    File f3 = LittleFS.open("/assets/coins/heads.wav", "r");
    if (f3)
    {
        g_wav_heads_len = f3.size() - 44;
        g_wav_heads = (uint8_t *)ps_malloc(g_wav_heads_len);
        if (g_wav_heads)
        {
            f3.seek(44);
            f3.read(g_wav_heads, g_wav_heads_len);
        }
        f3.close();
    }

    File f4 = LittleFS.open("/assets/coins/tails.wav", "r");
    if (f4)
    {
        g_wav_tails_len = f4.size() - 44;
        g_wav_tails = (uint8_t *)ps_malloc(g_wav_tails_len);
        if (g_wav_tails)
        {
            f4.seek(44);
            f4.read(g_wav_tails, g_wav_tails_len);
        }
        f4.close();
    }

    // 2. 【新增】：吸入所有 6 张硬币材质 (64*64*2 = 8KB，共 48KB)
    String prefixes[3] = {"/assets/coins/", "/assets/coins/r", "/assets/coins/g"};
    for (int i = 0; i < 3; i++)
    {
        g_img_heads[i] = (uint16_t *)ps_malloc(8192);
        g_img_tails[i] = (uint16_t *)ps_malloc(8192);

        File fh = LittleFS.open((prefixes[i] + "heads.bin").c_str(), "r");
        if (!fh)
            fh = LittleFS.open("/assets/coins/heads.bin", "r"); // 找不到就用金色兜底
        if (fh)
        {
            fh.read((uint8_t *)g_img_heads[i], 8192);
            fh.close();
        }

        File ft = LittleFS.open((prefixes[i] + "tails.bin").c_str(), "r");
        if (!ft)
            ft = LittleFS.open("/assets/coins/tails.bin", "r");
        if (ft)
        {
            ft.read((uint8_t *)g_img_tails[i], 8192);
            ft.close();
        }
    }
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