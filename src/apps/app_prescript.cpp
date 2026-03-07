// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "prescript_data.h"
#include "sys_auto_push.h"
#include "sys_config.h" // 【新增】：引入配置以便读取自定义指令池

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

    // 【修复 1】：增强缓冲容量，抵抗外部恶意超长指令
    int Split_To_Lines(const char* formatted_str, char lines[][256]) {
        int line_idx = 0, buf_idx = 0;
        for(int i = 0; formatted_str[i] != '\0'; i++) {
            if (formatted_str[i] == '\n') {
                lines[line_idx][buf_idx] = '\0';
                line_idx++; buf_idx = 0;
                if (line_idx >= 30) break; 
            } else {
                if (buf_idx < 255) lines[line_idx][buf_idx++] = formatted_str[i];
            }
        }
        if (buf_idx > 0 && line_idx < 30) { lines[line_idx][buf_idx] = '\0'; line_idx++; }
        return line_idx;
    }

    void Format_Chinese_To_Grid(const char *raw_str, char *formatted_buf) {
        int max_v = (HAL_Get_Screen_Width() - UI_MARGIN_LEFT) / 8; 
        int current_w = 0; int buf_idx = 0;
        for(int i = 0; raw_str[i] != '\0'; ) {
            // 安全处理显式换行
            if (raw_str[i] == '\n') {
                formatted_buf[buf_idx++] = raw_str[i++];
                current_w = 0; continue;
            }
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

    // 【修复 2】：重构英文单词换行与视觉宽度截断逻辑
    void Format_English_To_Grid(const char *raw_str, char *formatted_buf) {
        int max_v = (HAL_Get_Screen_Width() - 10) / 8;
        int current_w = 0; int buf_idx = 0;
        int last_space_i = -1; int last_space_buf = -1; int last_space_w = 0;
        
        for(int i = 0; raw_str[i] != '\0'; ) {
            // 兼容外部传来的强制换行
            if (raw_str[i] == '\n') {
                formatted_buf[buf_idx++] = raw_str[i++];
                current_w = 0; last_space_i = -1; last_space_buf = -1;
                continue;
            }
            int clen = get_utf8_char_len(raw_str[i]);
            if (raw_str[i] == ' ') { last_space_i = i; last_space_buf = buf_idx; last_space_w = current_w; }
            for(int b = 0; b < clen; b++) formatted_buf[buf_idx++] = raw_str[i++];
            
            current_w += (clen > 1) ? 2 : 1; 
            if (current_w >= max_v) {
                if (last_space_i != -1 && last_space_buf != -1 && (buf_idx - last_space_buf < 30)) {
                    formatted_buf[last_space_buf] = '\n';
                    current_w = current_w - last_space_w - 1; // 完美重置下一行的前置视觉宽度
                } else {
                    formatted_buf[buf_idx++] = '\n';
                    current_w = 0;
                }
                last_space_i = -1; last_space_buf = -1;
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

        // 统一限制最大绘图行数为 12 行，防止溢出屏幕显存！
        int lines = (sh - start_y) / 16; if (lines > 12) lines = 12;

        if (lang == LANG_ZH) {
            int max_visual_width = (sw - UI_MARGIN_LEFT) / 8; 
            for (int row = 0; row < lines; row++) {
                char row_buf[256]; int buf_idx = 0, current_w = 0;
                while (current_w < max_visual_width - 1) {
                    if (random(100) < 20 && current_w <= max_visual_width - 2) {
                        int pick = random(CACHE_SIZE);
                        row_buf[buf_idx++] = cached_glitch_pool[pick][0]; row_buf[buf_idx++] = cached_glitch_pool[pick][1]; row_buf[buf_idx++] = cached_glitch_pool[pick][2];
                        current_w += 2; 
                    } else {
                        row_buf[buf_idx++] = 33 + random(94); current_w += 1; 
                    }
                }
                row_buf[buf_idx] = '\0'; HAL_Screen_ShowChineseLine(10, start_y + row * 16, row_buf);
            }
        } else {
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
        
        if (__internal_prescript_mode == 3 || __internal_prescript_mode == 4) {
            rule = __internal_custom_prescript;
        } else {
            // 【修复 3】：恢复缺失的“混合指令池”抽取魔法！
            if (sysConfig.custom_prescript_count > 0 && random(100) < 30) {
                int pick = random(sysConfig.custom_prescript_count);
                rule = sysConfig.custom_prescripts[pick].c_str();
            } else {
                rule = Get_Prescript(current_lang, random(Get_Prescript_Count(current_lang)));
            }
        }
        
        static char raw_prescript[512]; static char formatted_buf[1024]; static char lines[30][256];
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0';
        int actual_lines = 0;
        int sw = HAL_Get_Screen_Width(); 
        
        // 【核心修复】：中英模式统一限制最多渲染 12 行，彻底切断崩溃根源！
        int draw_lines = 12;

        if (current_lang == LANG_ZH) {
            Format_Chinese_To_Grid(raw_prescript, formatted_buf);
            actual_lines = Split_To_Lines(formatted_buf, lines);
            int max_v = (sw - UI_MARGIN_LEFT) / 8;
            uint8_t lucky[15][40];
            for(int r=0; r<draw_lines; r++) for(int c=0; c<max_v; c++) lucky[r][c] = random(11) + 2;
            
            for(int frame=0; frame<22; frame++) {
                uint8_t all_locked=1; HAL_Sprite_Clear(); HAL_Screen_DrawHeader(); HAL_Draw_Line(0,UI_HEADER_HEIGHT,sw,UI_HEADER_HEIGHT,1);
                for(int r=0; r<draw_lines; r++) {
                    char row_buf[512]; int buf_idx=0, current_w=0, byte_idx=0;
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
            Format_English_To_Grid(raw_prescript, formatted_buf);
            actual_lines = Split_To_Lines(formatted_buf, lines);
            int max_v = (sw - 10) / 8;
            uint8_t lucky[15][40];
            for(int r=0; r<draw_lines; r++) for(int c=0; c<max_v; c++) lucky[r][c] = random(11) + 2;
            
            for(int frame=0; frame<22; frame++) {
                uint8_t all_locked=1; HAL_Sprite_Clear(); HAL_Screen_DrawHeader(); HAL_Draw_Line(0,UI_HEADER_HEIGHT,sw,UI_HEADER_HEIGHT,1);
                for(int r=0; r<draw_lines; r++) {
                    char row_buf[512]; int buf_idx=0, current_w=0, byte_idx=0;
                    if(r < actual_lines) {
                        while(lines[r][byte_idx]!='\0' && current_w < max_v) {
                            int clen = get_utf8_char_len(lines[r][byte_idx]);
                            int cw = (clen > 1) ? 2 : 1;
                            if(frame>=lucky[r][current_w]) { 
                                for(int b=0; b<clen; b++) row_buf[buf_idx++] = lines[r][byte_idx+b]; 
                            }
                            else { 
                                all_locked=0; 
                                if(cw==2) { 
                                    int p=random(CACHE_SIZE); 
                                    row_buf[buf_idx++]=cached_glitch_pool[p][0]; row_buf[buf_idx++]=cached_glitch_pool[p][1]; row_buf[buf_idx++]=cached_glitch_pool[p][2]; 
                                }
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