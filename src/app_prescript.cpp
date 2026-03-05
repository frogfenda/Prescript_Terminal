// 文件：src/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "prescript_logic.h"

class AppPrescript : public AppBase {
private:
    // ==== 专属的 FX 特效引擎与排版算法 ====
    void Format_Text_To_Grid(const char* raw_str, char* formatted_buf, int* out_len) {
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

    void Render_Lines(char* buf, uint16_t len, uint8_t start_y) {
        char line_buf[21];
        int lines = len / 20;
        for(int row = 0; row < lines; row++) {
            strncpy(line_buf, &buf[row * 20], 20);
            line_buf[20] = '\0';
            HAL_Screen_ShowTextLine(0, start_y + (row * 16), line_buf);
        }
        HAL_Screen_Update();
    }

    void Decode_Animation(const char* target_str, uint16_t len, uint8_t start_y, uint8_t max_frames) {
        char display_buf[265]; 
        uint8_t lucky_jumps[265]; 

        for (int i = 0; i < len; i++) {
            lucky_jumps[i] = random(max_frames / 2) + 2; 
        }
        display_buf[len] = '\0';

        for (int frame = 0; frame < max_frames; frame++) {
            uint8_t all_locked = 1; 
            for (int i = 0; i < len; i++) {
                if (frame >= lucky_jumps[i]) {
                    display_buf[i] = target_str[i]; 
                } else { 
                    display_buf[i] = 33 + random(94); 
                    all_locked = 0; 
                }
            }
            Render_Lines(display_buf, len, start_y);
            if (!all_locked) HAL_Buzzer_Random_Glitch();
            if (all_locked) break; 
            yield(); 
        }
    }

    void Engine_Chaos_Wait(void) {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();
        char row_buf[21];  
        row_buf[20] = '\0';
        
        while(HAL_Is_Key_Pressed()) { delay(10); yield(); }
        
        while(!HAL_Is_Key_Pressed()) {
            for(int row = 0; row < 13; row++) { 
                for(int i = 0; i < 20; i++) row_buf[i] = 33 + random(94); 
                HAL_Screen_ShowTextLine(0, row * 16, row_buf); 
            }
            HAL_Screen_Update(); 
            HAL_Buzzer_Random_Glitch();
            delay(15); 
            yield();
        }
        while(HAL_Is_Key_Pressed()) { delay(10); yield(); }
    }

    void Scroll_Text(const char* formatted_buf, int fmt_len) {
        if (fmt_len <= 260) return; 

        int current_idx = 260; 
        while (current_idx < fmt_len) {
            if (current_idx == 260) delay(500); 
            
            for(int step = 0; step < 8; step++) {
                HAL_Screen_Scroll_Up(2); 
                HAL_Screen_Update();
                yield(); 
            }
            HAL_Buzzer_Play_Tone(3000, 15); 
            
            char line_buf[21];
            strncpy(line_buf, &formatted_buf[current_idx], 20);
            line_buf[20] = '\0';
            
            Decode_Animation(line_buf, 20, 192, 10); 
            current_idx += 20;
        }
    }

    // ==== 核心执行序列 ====
    void executePrescriptSequence() {
        Engine_Chaos_Wait();
        
        // 【关键解耦点】：从纯净的 logic 层获取数据
        const char* rule = Prescript_GetRandomRule();
        
        char raw_prescript[512]; 
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0'; 
        
        char formatted_buf[1024]; 
        int fmt_len = 0;
        Format_Text_To_Grid(raw_prescript, formatted_buf, &fmt_len);
        
        char first_screen_buf[265]; 
        int first_chunk = (fmt_len > 260) ? 260 : fmt_len;
        strncpy(first_screen_buf, formatted_buf, first_chunk);
        for(int i = first_chunk; i < 260; i++) first_screen_buf[i] = ' '; 
        first_screen_buf[260] = '\0'; 
        
        Decode_Animation(first_screen_buf, 260, 0, 22); 
        Scroll_Text(formatted_buf, fmt_len);
        
        delay(100);
        HAL_Buzzer_Play_Tone(1500, 60); delay(30);
        HAL_Buzzer_Play_Tone(2000, 60); delay(30);
        HAL_Buzzer_Play_Tone(2500, 150);
    }

public:
    void onCreate() override {
        executePrescriptSequence();
    }

    void onLoop() override {}
    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override {
        HAL_Buzzer_Play_Tone(2200, 40);
        executePrescriptSequence();
    }

    void onKeyLong() override {
        appManager.launchApp(appMainMenu);
    }
};

AppPrescript instancePrescript;
AppBase* appPrescript = &instancePrescript;