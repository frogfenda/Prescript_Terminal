#include "prescript_logic.h"
#include "prescript_data.h"
#include "hal.h"
#include <Arduino.h>

static void Prescript_Decode_Effect(const char* target_str, uint8_t x, uint8_t y);
static void Prescript_Decode_Line(const char* target_str, uint8_t x, uint8_t y);

// =========================================================================
// [ 引擎 1：混沌待机引擎 ] 
// =========================================================================
static void Engine_Chaos_Wait(void) {
    Screen_Clear();
    Screen_DrawHeader();
    char row_buf[17];  
    row_buf[16] = '\0';
    uint16_t chaos_timeout = 0;
    
    while(Is_Key_Pressed()) delay(20);
    
    while(!Is_Key_Pressed()) {
        for(int row = 0; row < 4; row++) {
            for(int i = 0; i < 16; i++) row_buf[i] = 33 + random(94); 
            // 每一行 16 个像素高，从 x=10 稍微留点左边距开始画
            Screen_ShowTextLine(10, row * 16, row_buf);
        }
        Screen_Update(); 
        Buzzer_Random_Glitch();
        delay(5); 
        
        if(++chaos_timeout > 3000) break; 
    }
    while(Is_Key_Pressed()) delay(20);
}

// =========================================================================
// [ 引擎 2：智能排版引擎 ] (核心算法一行不改)
// =========================================================================
static void Engine_Format_Text(const char* raw_str, char* formatted_buf, int* out_len) {
    int total_src_len = strlen(raw_str);
    int fmt_len = 0; 
    int src_idx = 0;

    while (src_idx < total_src_len) {
        while (raw_str[src_idx] == ' ') src_idx++; 
        if (src_idx >= total_src_len) break;

        int chunk = total_src_len - src_idx;
        if (chunk > 16) {
            if (raw_str[src_idx + 16] == ' ' || raw_str[src_idx + 16] == '\0') {
                chunk = 16;
            } else {
                int space_idx = -1;
                for (int i = 15; i > 0; i--) {
                    if (raw_str[src_idx + i] == ' ') { space_idx = i; break; }
                }
                chunk = (space_idx != -1) ? space_idx : 16; 
            }
        }

        for (int i = 0; i < 16; i++) {
            formatted_buf[fmt_len++] = (i < chunk) ? raw_str[src_idx + i] : ' ';
        }
        src_idx += chunk; 
    }
    formatted_buf[fmt_len] = '\0'; 
    *out_len = fmt_len; 
}

// =========================================================================
// [ 引擎 3：首屏解密特效 ]
// =========================================================================
static void Prescript_Decode_Effect(const char* target_str, uint8_t x, uint8_t y) {
    uint8_t len = strlen(target_str);
    char display_buf[80]; 
    int lucky_jumps[80]; 
    
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

        // 把长字符串切成 4 行画到显存精灵上
        for(int row = 0; row < 4; row++) {
            char line_buf[17];
            strncpy(line_buf, &display_buf[row * 16], 16);
            line_buf[16] = '\0';
            Screen_ShowTextLine(10, row * 16, line_buf);
        }
        Screen_Update();

        if (!all_locked) Buzzer_Random_Glitch();
        if (all_locked) break; 
    }
}

// =========================================================================
// [ 引擎 4：单行解密与滚动 ] (降维打击：直接调用 Screen_Scroll_Up)
// =========================================================================
static void Prescript_Decode_Line(const char* target_str, uint8_t x, uint8_t y) {
    uint8_t len = strlen(target_str);
    char display_buf[20]; 
    int lucky_jumps[20]; 

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
    }
}

static void Engine_Scroll_Text(const char* formatted_buf, int fmt_len) {
    if (fmt_len <= 64) return; 

    int current_idx = 64; 
    while (current_idx < fmt_len) {
        
        if (current_idx == 64) delay(2000); 
        else delay(600);                  
        
        // 【降维打击】：C++ 库自带的硬件级像素卷轴
        Screen_Scroll_Up(16);
        Screen_Update();
        Buzzer_Play_Tone(3000, 30); 
        
        char line_buf[17];
        strncpy(line_buf, &formatted_buf[current_idx], 16);
        line_buf[16] = '\0';
        
        // 新行固定在第 4 行 (y=48) 解密
        Prescript_Decode_Line(line_buf, 10, 48);
        current_idx += 16;
    }
}

// =========================================================================
// [ 总指挥 ]
// =========================================================================
void Prescript_Action(void) {
    Engine_Chaos_Wait();
    
    int index = random(Get_Prescript_Count());
    char raw_prescript[256]; 
    sprintf(raw_prescript, "_%s_", Get_Prescript(index));
    
    char formatted_buf[512]; 
    int fmt_len = 0;
    Engine_Format_Text(raw_prescript, formatted_buf, &fmt_len);
    
    char first_screen_buf[65]; 
    int first_chunk = (fmt_len > 64) ? 64 : fmt_len;
    strncpy(first_screen_buf, formatted_buf, first_chunk);
    for(int i = first_chunk; i < 64; i++) first_screen_buf[i] = ' '; 
    first_screen_buf[64] = '\0'; 
    
    Prescript_Decode_Effect(first_screen_buf, 0, 0);
    Engine_Scroll_Text(formatted_buf, fmt_len);
    
    delay(200);
    Buzzer_Play_Tone(1500, 100); delay(50);
    Buzzer_Play_Tone(2000, 100); delay(50);
    Buzzer_Play_Tone(2500, 200);
}