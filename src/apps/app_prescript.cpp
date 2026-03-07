// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "prescript_data.h"
#include "sys_auto_push.h"

class AppPrescript : public AppBase {
private:
    const char *GLOBAL_CHINESE_DICT = "数据错误系统异常致命警告内存溢出未知指令核心损坏参数丢失连接中断身份拒绝权限不足矩阵终端序列重载覆写剥离重构解析编译代码节点网关漏洞端口渗透代理溯源封锁拦截劫持";

    static const int CACHE_SIZE = 300;
    char cached_glitch_pool[CACHE_SIZE][4]; 
    
    enum State { S_INIT, S_WAIT_RELEASE, S_CHAOS, S_DECODE, S_DONE };
    State m_state;

    void Init_Glitch_Pool() {
        int total_chars = strlen(GLOBAL_CHINESE_DICT) / 3;
        for(int i = 0; i < CACHE_SIZE; i++) {
            int pick = random(total_chars) * 3;
            cached_glitch_pool[i][0] = GLOBAL_CHINESE_DICT[pick];
            cached_glitch_pool[i][1] = GLOBAL_CHINESE_DICT[pick+1];
            cached_glitch_pool[i][2] = GLOBAL_CHINESE_DICT[pick+2];
            cached_glitch_pool[i][3] = '\0';
        }
    }

    int get_utf8_char_len(char c) {
        unsigned char uc = (unsigned char)c;
        if ((uc & 0x80) == 0) return 1; 
        if ((uc & 0xE0) == 0xC0) return 2; 
        if ((uc & 0xF0) == 0xE0) return 3; 
        if ((uc & 0xF8) == 0xF0) return 4; 
        return 1;
    }

    int Split_To_Lines(const char* formatted_str, char lines[][128]) {
        int line_idx = 0, buf_idx = 0;
        for(int i = 0; formatted_str[i] != '\0'; i++) {
            if (formatted_str[i] == '\n') {
                lines[line_idx][buf_idx] = '\0';
                line_idx++; buf_idx = 0;
                if (line_idx >= 30) break; 
            } else {
                lines[line_idx][buf_idx++] = formatted_str[i];
            }
        }
        if (buf_idx > 0 && line_idx < 30) { lines[line_idx][buf_idx] = '\0'; line_idx++; }
        return line_idx;
    }

    // ========================================================
    // 【终极修复】：恢复中英双语的防溢出硬核排版引擎！
    // ========================================================
    void Format_Chinese_To_Grid(const char *raw_str, char *formatted_buf) {
        int max_v = (HAL_Get_Screen_Width() - UI_MARGIN_LEFT) / 8; 
        int current_w = 0; int buf_idx = 0;
        for(int i = 0; raw_str[i] != '\0'; ) {
            int clen = get_utf8_char_len(raw_str[i]);
            int cw = (clen > 1) ? 2 : 1; 
            if (current_w + cw > max_v - 1) {
                formatted_buf[buf_idx++] = '\n';
                current_w = 0;
            }
            for(int b = 0; b < clen; b++) formatted_buf[buf_idx++] = raw_str[i++];
            current_w += cw;
        }
        formatted_buf[buf_idx] = '\0';
    }

    void Format_English_To_Grid(const char *raw_str, char *formatted_buf) {
        int max_v = (HAL_Get_Screen_Width() - 10) / 8;
        int current_w = 0; int buf_idx = 0;
        int last_space_i = -1; int last_space_buf = -1;
        
        for(int i = 0; raw_str[i] != '\0'; ) {
            int clen = get_utf8_char_len(raw_str[i]);
            if (raw_str[i] == ' ') { last_space_i = i; last_space_buf = buf_idx; }
            for(int b = 0; b < clen; b++) formatted_buf[buf_idx++] = raw_str[i++];
            
            current_w += (clen > 1) ? 2 : 1; 
            if (current_w >= max_v) {
                // 英文优先按空格换行，防止单词被粗暴切断
                if (last_space_i != -1 && last_space_buf != -1 && (buf_idx - last_space_buf < 20)) {
                    formatted_buf[last_space_buf] = '\n';
                    current_w = buf_idx - last_space_buf - 1;
                } else {
                    formatted_buf[buf_idx++] = '\n';
                    current_w = 0;
                }
                last_space_i = -1;
            }
        }
        formatted_buf[buf_idx] = '\0';
    }

    void drawChaosFrame(SystemLang_t lang) {
        int sw = HAL_Get_Screen_Width(); int sh = HAL_Get_Screen_Height();
        int start_y = UI_HEADER_HEIGHT + 10; 
        HAL_Sprite_Clear();
        HAL_Screen_DrawHeader();
        HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

        if (lang == LANG_ZH) {
            int lines = (sh - start_y) / 16; if (lines > 12) lines = 12;
            int max_visual_width = (sw - UI_MARGIN_LEFT) / 8; 
            for (int row = 0; row < lines; row++) {
                char row_buf[256]; int buf_idx = 0, current_w = 0;
                while (current_w < max_visual_width - 1) {
                    if (random(100) < 20 && current_w <= max_visual_width - 2) {
                        int pick = random(CACHE_SIZE);
                        row_buf[buf_idx++] = cached_glitch_pool[pick][0];
                        row_buf[buf_idx++] = cached_glitch_pool[pick][1];
                        row_buf[buf_idx++] = cached_glitch_pool[pick][2];
                        current_w += 2; 
                    } else {
                        row_buf[buf_idx++] = 33 + random(94); current_w += 1; 
                    }
                }
                row_buf[buf_idx] = '\0';
                HAL_Screen_ShowChineseLine(10, start_y + row * 16, row_buf);
            }
        } else {
            int lines = (sh - start_y) / 16; if (lines > 15) lines = 15;
            int chars_per_line = (sw - 10) / 8; 
            for (int row = 0; row < lines; row++) {
                char row_buf[chars_per_line + 1];
                for (int i = 0; i < chars_per_line; i++) row_buf[i] = 33 + random(94);
                row_buf[chars_per_line] = '\0';
                HAL_Screen_ShowTextLine(5, start_y + row * 16, row_buf);
            }
        }
        HAL_Screen_Update(); SYS_SOUND_GLITCH(); 
    }

    void executeDecodeSequence() {
        SystemLang_t current_lang = appManager.getLanguage();
        extern int __internal_prescript_mode; 
        extern char __internal_custom_prescript[512];
        const char *rule;
        
        if (__internal_prescript_mode == 3 || __internal_prescript_mode == 4) rule = __internal_custom_prescript;
        else rule = Get_Prescript(current_lang, random(Get_Prescript_Count(current_lang)));
        
        static char raw_prescript[512]; static char formatted_buf[1024]; static char lines[30][128];
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0';
        int actual_lines = 0;
        int sw = HAL_Get_Screen_Width(); 

        if (current_lang == LANG_ZH) {
            Format_Chinese_To_Grid(raw_prescript, formatted_buf);
            actual_lines = Split_To_Lines(formatted_buf, lines);
            int max_v = (sw - UI_MARGIN_LEFT) / 8;
            uint8_t lucky[15][40];
            for(int r=0; r<12; r++) for(int c=0; c<max_v; c++) lucky[r][c] = random(11) + 2;
            
            for(int frame=0; frame<22; frame++) {
                uint8_t all_locked=1; HAL_Sprite_Clear(); HAL_Screen_DrawHeader(); HAL_Draw_Line(0,UI_HEADER_HEIGHT,sw,UI_HEADER_HEIGHT,1);
                for(int r=0; r<12; r++) {
                    char row_buf[256]; int buf_idx=0, current_w=0, byte_idx=0;
                    if(r < actual_lines) {
                        while(lines[r][byte_idx]!='\0' && current_w<max_v-1) {
                            int clen=get_utf8_char_len(lines[r][byte_idx]); int cw=(clen>1)?2:1;
                            if(frame>=lucky[r][current_w]) { for(int b=0; b<clen; b++) row_buf[buf_idx++]=lines[r][byte_idx+b]; }
                            else { all_locked=0; if(cw==2){ int p=random(CACHE_SIZE); row_buf[buf_idx++]=cached_glitch_pool[p][0]; row_buf[buf_idx++]=cached_glitch_pool[p][1]; row_buf[buf_idx++]=cached_glitch_pool[p][2]; }else row_buf[buf_idx++]=33+random(94); }
                            current_w+=cw; byte_idx+=clen;
                        }
                    }
                    while(current_w<max_v-1) {
                        if(frame<lucky[r][current_w]) { all_locked=0; if(random(100)<20 && current_w<=max_v-2){ int p=random(CACHE_SIZE); row_buf[buf_idx++]=cached_glitch_pool[p][0]; row_buf[buf_idx++]=cached_glitch_pool[p][1]; row_buf[buf_idx++]=cached_glitch_pool[p][2]; current_w+=2; }else{ row_buf[buf_idx++]=33+random(94); current_w+=1; } }
                        else { row_buf[buf_idx++]=' '; current_w+=1; }
                    }
                    row_buf[buf_idx]='\0'; if(buf_idx>0) HAL_Screen_ShowChineseLine(10, UI_HEADER_HEIGHT+10+r*16, row_buf);
                }
                HAL_Screen_Update(); if(!all_locked) SYS_SOUND_GLITCH(); if(all_locked) break; yield();
            }
        } else {
            // 【彻底恢复】：被省略掉的纯正英文解码序列
            Format_English_To_Grid(raw_prescript, formatted_buf);
            actual_lines = Split_To_Lines(formatted_buf, lines);
            int max_v = (sw - 10) / 8;
            uint8_t lucky[15][40];
            for(int r=0; r<15; r++) for(int c=0; c<max_v; c++) lucky[r][c] = random(11) + 2;
            
            for(int frame=0; frame<22; frame++) {
                uint8_t all_locked=1; HAL_Sprite_Clear(); HAL_Screen_DrawHeader(); HAL_Draw_Line(0,UI_HEADER_HEIGHT,sw,UI_HEADER_HEIGHT,1);
                for(int r=0; r<15; r++) {
                    char row_buf[256]; int buf_idx=0, current_w=0, byte_idx=0;
                    if(r < actual_lines) {
                        while(lines[r][byte_idx]!='\0' && current_w < max_v) {
                            int clen = get_utf8_char_len(lines[r][byte_idx]);
                            int cw = (clen > 1) ? 2 : 1;
                            if(frame>=lucky[r][current_w]) { for(int b=0; b<clen; b++) row_buf[buf_idx++] = lines[r][byte_idx+b]; }
                            else { 
                                all_locked=0; 
                                if(cw==2) { int p=random(CACHE_SIZE); row_buf[buf_idx++]=cached_glitch_pool[p][0]; row_buf[buf_idx++]=cached_glitch_pool[p][1]; row_buf[buf_idx++]=cached_glitch_pool[p][2]; }
                                else row_buf[buf_idx++] = 33+random(94); 
                            }
                            current_w += cw; byte_idx += clen;
                        }
                    }
                    while(current_w < max_v) {
                        if(frame<lucky[r][current_w]) { all_locked=0; row_buf[buf_idx++] = 33+random(94); }
                        else { row_buf[buf_idx++] = ' '; }
                        current_w++;
                    }
                    row_buf[buf_idx]='\0'; 
                    if(buf_idx>0) HAL_Screen_ShowTextLine(5, UI_HEADER_HEIGHT+10+r*16, row_buf);
                }
                HAL_Screen_Update(); if(!all_locked) SYS_SOUND_GLITCH(); if(all_locked) break; yield();
            }
        }
        delay(100); HAL_Buzzer_Play_Tone(1500, 60);
        delay(30); HAL_Buzzer_Play_Tone(2000, 60);
        delay(30); SYS_SOUND_CONFIRM(); 
        SysAutoPush_ResetTimer();
    }

public:
    void onCreate() override { 
        Init_Glitch_Pool(); 
        extern int __internal_prescript_mode;
        if (__internal_prescript_mode != 2 && __internal_prescript_mode != 3) {
            m_state = S_WAIT_RELEASE; 
        } else {
            m_state = S_DECODE;
        }
    }
    
    void onLoop() override {
        if (m_state == S_WAIT_RELEASE) {
            if (!HAL_Is_Key_Pressed()) m_state = S_CHAOS;
        }
        else if (m_state == S_CHAOS) {
            drawChaosFrame(appManager.getLanguage());
            delay(15); 
        }
        else if (m_state == S_DECODE) {
            executeDecodeSequence();
            m_state = S_DONE;
        }
    }
    
    void onDestroy() override {
        extern int __internal_prescript_mode;
        __internal_prescript_mode = 0; 
    }
    void onKnob(int delta) override {}

    void onKeyShort() override { 
        if (m_state == S_CHAOS) {
            SYS_SOUND_NAV(); 
            m_state = S_DECODE; 
        }
        else if (m_state == S_DONE) {
            SYS_SOUND_NAV(); 
            extern int __internal_prescript_mode;
            if (__internal_prescript_mode == 0) m_state = S_WAIT_RELEASE; 
            else appManager.popApp();      
        }
    } 
    
    void onKeyLong() override { 
        if (m_state == S_DONE || m_state == S_CHAOS) appManager.popApp(); 
    }
};

int __internal_prescript_mode = 0; 
char __internal_custom_prescript[512] = {0}; 
void Prescript_Launch_PushNormal() { __internal_prescript_mode = 1; }
void Prescript_Launch_PushDirect() { __internal_prescript_mode = 2; }
void Prescript_Launch_Custom(const char* custom_text) {
    __internal_prescript_mode = 3;
    snprintf(__internal_custom_prescript, sizeof(__internal_custom_prescript), "%s", custom_text);
}
void Prescript_Launch_Custom_Wait(const char* custom_text) {
    __internal_prescript_mode = 4;
    snprintf(__internal_custom_prescript, sizeof(__internal_custom_prescript), "%s", custom_text);
}

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;