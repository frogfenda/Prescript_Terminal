// 文件：src/sys/sys_res.h
#pragma once
#include <Arduino.h>

extern uint8_t* g_wav_procedure;
extern uint32_t g_wav_procedure_len;
extern uint8_t* g_wav_final;
extern uint32_t g_wav_final_len;

extern uint8_t* g_wav_heads;
extern uint32_t g_wav_heads_len;
extern uint8_t* g_wav_tails;
extern uint32_t g_wav_tails_len;
extern uint8_t* g_ahab_sound;
extern uint32_t g_ahab_sound_len;

extern uint16_t* g_img_heads[3];
extern uint16_t* g_img_tails[3];

// ==========================================
// 【新增】：提取部数据结构与全局池指针
// ==========================================
struct IdentityData {
    String sinner;
    String id_name;
    int star;
    int walp;
};

extern IdentityData* g_gacha_pool;
extern int g_gacha_pool_total;
extern int* g_gacha_1star; extern int g_count_1star;
extern int* g_gacha_2star; extern int g_count_2star;
extern int* g_gacha_3star; extern int g_count_3star;

void SysRes_Init();