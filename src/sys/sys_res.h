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

// 资源初始化引擎
void SysRes_Init();