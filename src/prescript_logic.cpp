#include "prescript_logic.h"
#include "prescript_data.h"
#include "hal.h"
#include <Arduino.h>

static void Prescript_Decode_Effect(const char* target_str, uint8_t x, uint8_t y);
static void Prescript_Decode_Line(const char* target_str, uint8_t x, uint8_t y);

static void Engine_Chaos_Wait(void) {
    Screen_Clear();
    Screen_DrawHeader();
    char row_buf[21];  
    row_buf[20] = '\0';
    uint16_t chaos_timeout = 0;
    
    while(Is_Key_Pressed()) delay(20);
    
    while(!Is_Key_Pressed()) {
        // 【修改】：画满 13 行！
        for(int row = 0; row < 13; row++) { 
            for(int i = 0; i < 20; i++) row_buf[i] = 33 + random(94); 
            Screen_ShowTextLine(0, row * 16, row_buf); 
        }
        Screen_Update(); 
        Buzzer_Random_Glitch();
        delay(5); 
        
        if(++chaos_timeout > 3000) break; 
    }
    while(Is_Key_Pressed()) delay(20);
}

static void Engine_Format_Text(const char* raw_str, char* formatted_buf, int* out_len) {
    int total_src_len = strlen(raw_str);
    int fmt_len = 0; 
    int src_idx = 0;

    while (src_idx < total_src_len) {
        while (raw_str[src_idx] == ' ') src_idx++; 
        if (src_idx >= total_src_len) break;

        int chunk = total_src_len - src_idx;
        if (chunk > 20) {
            if (raw_str[src_idx + 20] == ' ' || raw_str[src_idx + 20] == '\0') {
                chunk = 20;
            } else {
                int space_idx = -1;
                for (int i = 19; i > 0; i--) { 
                    if (raw_str[src_idx + i] == ' ') { space_idx = i; break; }
                }
                chunk = (space_idx != -1) ? space_idx : 20; 
            }
        }

        for (int i = 0; i < 20; i++) {
            formatted_buf[fmt_len++] = (i < chunk) ? raw_str[src_idx + i] : ' ';
        }
        src_idx += chunk; 
    }
    formatted_buf[fmt_len] = '\0'; 
    *out_len = fmt_len; 
}

static void Prescript_Decode_Effect(const char* target_str, uint8_t x, uint8_t y) {
    // 【修复 1】：将 uint8_t 改为 uint16_t，防止 260 个字符导致长度溢出
    uint16_t len = strlen(target_str); 
    
    // 【修复 2】：加入 static，将巨型数组转移出堆栈区
    static char display_buf[265]; 
    
    // 【修复 3】：将 int (4字节) 改为 uint8_t (1字节)，内存消耗从 1060 爆降至 265！
    static uint8_t lucky_jumps[265]; 
    
    for (int i = 0; i < len; i++) {
        display_buf[i] = ' ';
        lucky_jumps[i] = (target_str[i] == ' ') ? 0 : (random(15) + 5); 
    }
    display_buf[len] = '\0';

    for (int frame = 0; frame < 25; frame++) {
        uint8_t all_locked = 1; 

        for (int i = 0; i < len; i++) {
            if (target_str[i] == ' ') continue;
            if (frame >= lucky_jumps[i]) display_buf[i] = target_str[i];
            else { display_buf[i] = 33 + random(94); all_locked = 0; }
        }

        for(int row = 0; row < 13; row++) {
            char line_buf[21];
            strncpy(line_buf, &display_buf[row * 20], 20);
            line_buf[20] = '\0';
            Screen_ShowTextLine(0, row * 16, line_buf);
        }
        Screen_Update();

        if (!all_locked) Buzzer_Random_Glitch();
        if (all_locked) break; 
        
        delay(30); // 喂狗，防止刷新过快死机
    }
}

// =========================================================================
// [ 引擎 4：单行解密与滚动卷轴 ] 内存安全重构版
// =========================================================================
static void Prescript_Decode_Line(const char* target_str, uint8_t x, uint8_t y) {
    uint16_t len = strlen(target_str); // 【修复 1】：同步修改为 uint16_t
    char display_buf[25]; 
    uint8_t lucky_jumps[25]; // 【修复 2】：节约内存

    for (int i = 0; i < len; i++) {
        display_buf[i] = ' ';
        lucky_jumps[i] = (target_str[i] == ' ') ? 0 : (random(10) + 5); 
    }
    display_buf[len] = '\0';

    for (int frame = 0; frame < 20; frame++) {
        uint8_t all_locked = 1;
        for (int i = 0; i < len; i++) {
            if (target_str[i] == ' ') continue;
            if (frame >= lucky_jumps[i]) display_buf[i] = target_str[i];
            else { display_buf[i] = 33 + random(94); all_locked = 0; }
        }

        Screen_ShowTextLine(x, y, display_buf);
        Screen_Update();

        if (!all_locked) Buzzer_Random_Glitch();
        if (all_locked) break; 
        
        delay(30);
    }
}

static void Engine_Scroll_Text(const char* formatted_buf, int fmt_len) {
    // 【修改】：超过 13 行 (260 字符) 才触发滚动
    if (fmt_len <= 260) return; 

    int current_idx = 260; 
    while (current_idx < fmt_len) {
        
        if (current_idx == 260) delay(1500); 
        else delay(600);                  
        
        Screen_Scroll_Up(16);
        Screen_Update();
        Buzzer_Play_Tone(3000, 30); 
        
        char line_buf[21];
        strncpy(line_buf, &formatted_buf[current_idx], 20);
        line_buf[20] = '\0';
        
        // 【修改】：第 13 行的坐标是 12 * 16 = 192 
        Prescript_Decode_Line(line_buf, 0, 192);
        current_idx += 20;
    }
}

void Prescript_Action(void) {
    Engine_Chaos_Wait();
    
    int index = random(Get_Prescript_Count());
    
    // 【修复 4】：为下面三个巨型字符串缓冲区加上 static，彻底解决栈溢出！
    static char raw_prescript[512]; 
    sprintf(raw_prescript, "_%s_", Get_Prescript(index));
    
    static char formatted_buf[1024]; 
    int fmt_len = 0;
    Engine_Format_Text(raw_prescript, formatted_buf, &fmt_len);
    
    static char first_screen_buf[265]; 
    int first_chunk = (fmt_len > 260) ? 260 : fmt_len;
    strncpy(first_screen_buf, formatted_buf, first_chunk);
    for(int i = first_chunk; i < 260; i++) first_screen_buf[i] = ' '; 
    first_screen_buf[260] = '\0'; 
    
    Prescript_Decode_Effect(first_screen_buf, 0, 0);
    Engine_Scroll_Text(formatted_buf, fmt_len);
    
    delay(200);
    Buzzer_Play_Tone(1500, 100); delay(50);
    Buzzer_Play_Tone(2000, 100); delay(50);
    Buzzer_Play_Tone(2500, 200);
}