// 文件：src/sys/sys_res.cpp
#include "sys_res.h"
#include <LittleFS.h>

// 真正定义指针的地方
uint8_t* g_wav_procedure = nullptr;
uint32_t g_wav_procedure_len = 0;
uint8_t* g_wav_final = nullptr;
uint32_t g_wav_final_len = 0;

uint8_t* g_wav_heads = nullptr;
uint32_t g_wav_heads_len = 0;
uint8_t* g_wav_tails = nullptr;
uint32_t g_wav_tails_len = 0;

void SysRes_Init() {
    Serial.println("[资源管家] 正在将高清材质吸入 PSRAM 常驻...");
    
    File f1 = LittleFS.open("/assets/procedure.wav", "r");
    if (f1) {
        g_wav_procedure_len = f1.size() - 44;
        g_wav_procedure = (uint8_t*)ps_malloc(g_wav_procedure_len);
        if(g_wav_procedure) { f1.seek(44); f1.read(g_wav_procedure, g_wav_procedure_len); }
        f1.close();
    }
    
    File f2 = LittleFS.open("/assets/final.wav", "r");
    if (f2) {
        g_wav_final_len = f2.size() - 44;
        g_wav_final = (uint8_t*)ps_malloc(g_wav_final_len);
        if(g_wav_final) { f2.seek(44); f2.read(g_wav_final, g_wav_final_len); }
        f2.close();
    }
    
    File f3 = LittleFS.open("/assets/coins/heads.wav", "r");
    if (f3) {
        g_wav_heads_len = f3.size() - 44;
        g_wav_heads = (uint8_t*)ps_malloc(g_wav_heads_len);
        if(g_wav_heads) { f3.seek(44); f3.read(g_wav_heads, g_wav_heads_len); }
        f3.close();
    }
    
    File f4 = LittleFS.open("/assets/coins/tails.wav", "r");
    if (f4) {
        g_wav_tails_len = f4.size() - 44;
        g_wav_tails = (uint8_t*)ps_malloc(g_wav_tails_len);
        if(g_wav_tails) { f4.seek(44); f4.read(g_wav_tails, g_wav_tails_len); }
        f4.close();
    }
    Serial.println("[资源管家] 常驻资产挂载完毕！");
}