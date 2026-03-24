// 文件：src/sys/sys_res.h
#pragma once
#include <Arduino.h>

// 声明全局资源指针（只声明，不分配）
extern uint8_t* g_wav_procedure;
extern uint32_t g_wav_procedure_len;
extern uint8_t* g_wav_final;
extern uint32_t g_wav_final_len;

extern uint8_t* g_wav_heads;
extern uint32_t g_wav_heads_len;
extern uint8_t* g_wav_tails;
extern uint32_t g_wav_tails_len;
// 【新增】：3种颜色（金、红、绿）的硬币贴图全局指针
extern uint16_t* g_img_heads[3];
extern uint16_t* g_img_tails[3];


// 资源初始化引擎
void SysRes_Init();