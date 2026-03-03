#include "prescript_logic.h"
#include "prescript_data.h"
#include "hal.h"
#include <Arduino.h>

// =========================================================================
// [ 模块 1：数据排版引擎 ] (纯逻辑解耦，负责把文本切成 20 字一行的标准块)
// =========================================================================
static void Format_Text_To_Grid(const char* raw_str, char* formatted_buf, int* out_len) {
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

// =========================================================================
// [ 模块 2：FX 视觉特效引擎 ] (统一图形渲染，解耦业务逻辑)
// =========================================================================
namespace FX_Engine {
    
    // 渲染工具：将一维数组切成多行画上去
    void Render_Lines(char* buf, uint16_t len, uint8_t start_y) {
        char line_buf[21];
        int lines = len / 20;
        for(int row = 0; row < lines; row++) {
            strncpy(line_buf, &buf[row * 20], 20);
            line_buf[20] = '\0';
            Screen_ShowTextLine(0, start_y + (row * 16), line_buf);
        }
        Screen_Update();
    }
    // 【极速版：统一解密算法】完美合并了之前的两个解密函数！
    void Decode_Animation(const char* target_str, uint16_t len, uint8_t start_y, uint8_t max_frames) {
        static char display_buf[265]; 
        static uint8_t lucky_jumps[265]; 

        // 【修改 1】：为所有字符（哪怕是空格）分配一个随机锁定帧！
        for (int i = 0; i < len; i++) {
            lucky_jumps[i] = random(max_frames / 2) + 2; 
        }
        display_buf[len] = '\0';

        for (int frame = 0; frame < max_frames; frame++) {
            uint8_t all_locked = 1; 
            for (int i = 0; i < len; i++) {
                // 【修改 2】：去掉了原来的 continue，哪怕目标是空格，时间没到之前也必须显示乱码！
                if (frame >= lucky_jumps[i]) {
                    display_buf[i] = target_str[i]; 
                } else { 
                    display_buf[i] = 33 + random(94); 
                    all_locked = 0; 
                }
            }

            Render_Lines(display_buf, len, start_y);

            if (!all_locked) Buzzer_Random_Glitch();
            if (all_locked) break; 
            
            yield(); 
        }
    }
    // 待机乱码动画
    static void Engine_Chaos_Wait(void) {
    // 第一次按下后：立即抹掉图片，画上红色的 [ PRESCRIPT ] 抬头
    Screen_Clear();
    Screen_DrawHeader();
    char row_buf[21];  
    row_buf[20] = '\0';
    
    // 防误触：确保你刚才按下的手已经松开了
    while(Is_Key_Pressed()) { delay(10); yield(); }
    
    // 【核心死循环】：疯狂滚动乱码，直到你【第二次】按下按键！
    while(!Is_Key_Pressed()) {
        for(int row = 0; row < 13; row++) { 
            for(int i = 0; i < 20; i++) row_buf[i] = 33 + random(94); 
            Screen_ShowTextLine(0, row * 16, row_buf); 
        }
        Screen_Update(); 
        Buzzer_Random_Glitch();
        delay(15); 
        yield();
    }
    
    // 第二次按下后：确保你松手，然后退出乱码函数，往下继续执行极限解密！
    while(Is_Key_Pressed()) { delay(10); yield(); }
}
    void Scroll_Text(const char* formatted_buf, int fmt_len) {
        if (fmt_len <= 260) return; 

        int current_idx = 260; 
        while (current_idx < fmt_len) {
            
            if (current_idx == 260) delay(500); 
            
            // ====================================================
            // 【平滑卷轴魔法】：不再一次性跳 16 像素！
            // 把它分成 8 帧动画，每帧只往上滑动 2 个像素！
            // 在 160MHz 算力下，这会产生极其丝滑的推拉物理感！
            // ====================================================
            for(int step = 0; step < 8; step++) {
                Screen_Scroll_Up(2); 
                Screen_Update();
                yield(); 
            }
            
            Buzzer_Play_Tone(3000, 15); 
            
            char line_buf[21];
            strncpy(line_buf, &formatted_buf[current_idx], 20);
            line_buf[20] = '\0';
            
            Decode_Animation(line_buf, 20, 192, 10); 
            current_idx += 20;
        }
    }
}

// =========================================================================
// [ 模块 3：业务总指挥 ] (解耦后，主入口变得极其清晰简单)
// =========================================================================
void Prescript_Action(void) {
    FX_Engine::Engine_Chaos_Wait();
    
    int index = random(Get_Prescript_Count());
    
    static char raw_prescript[512]; 
    sprintf(raw_prescript, "_%s_", Get_Prescript(index));
    
    static char formatted_buf[1024]; 
    int fmt_len = 0;
    Format_Text_To_Grid(raw_prescript, formatted_buf, &fmt_len);
    
    static char first_screen_buf[265]; 
    int first_chunk = (fmt_len > 260) ? 260 : fmt_len;
    strncpy(first_screen_buf, formatted_buf, first_chunk);
    for(int i = first_chunk; i < 260; i++) first_screen_buf[i] = ' '; 
    first_screen_buf[260] = '\0'; 
    
    // 首屏特效：一次性解密 260 个字符，最多渲染 22 帧
    FX_Engine::Decode_Animation(first_screen_buf, 260, 0, 22); 
    
    // 卷轴滚动
    FX_Engine::Scroll_Text(formatted_buf, fmt_len);
    
    // 任务下达完毕的终结音效
    delay(100);
    Buzzer_Play_Tone(1500, 60); delay(30);
    Buzzer_Play_Tone(2000, 60); delay(30);
    Buzzer_Play_Tone(2500, 150);
}