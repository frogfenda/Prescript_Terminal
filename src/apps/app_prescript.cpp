// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "prescript_data.h"
#include "sys_auto_push.h"
class AppPrescript : public AppBase
{
private:
    const char *GLOBAL_CHINESE_DICT = 
        "数据错误系统异常致命警告内存溢出未知指令核心损坏参数丢失连接中断身份拒绝权限不足"
        "故障排查量子纠缠神经漫游黑客帝国赛博朋克深网协议底层逻辑防火墙破高维碎片意识上传"
        "天地玄黄宇宙洪荒日月盈仄辰宿列张寒来暑往秋收冬藏闰余成岁律吕调阳云腾致雨露结为霜"
        "金生丽水玉出昆冈剑号巨阙珠称夜光果珍李柰菜重芥姜海咸河淡鳞潜羽翔龙师火帝鸟官人皇"
        "始制文字乃服衣裳推位让国有虞陶唐吊民伐罪周发殷汤坐朝问道垂拱平章爱育黎首臣伏戎羌"
        "遐迩一体率宾归王鸣凤在竹白驹食场化被草木赖及万方盖此身发四大五常恭惟鞠养岂敢毁伤"
        "的了一是不我在有个人这上中大为生到以和国地出就其年还可能下多方于成这也同用发会起"
        "作前三部明长种无民理从实度机法新意常老两体制表当与化动把事心看天问正而点体面通命"
        "矩阵终端序列重载覆写剥离重构解析编译代码节点网关漏洞端口渗透代理溯源封锁拦截劫持";

    static const int CACHE_SIZE = 300;
    char cached_glitch_pool[CACHE_SIZE][4]; 

    void Init_Glitch_Pool()
    {
        int total_chars = strlen(GLOBAL_CHINESE_DICT) / 3;
        for(int i = 0; i < CACHE_SIZE; i++) {
            int pick = random(total_chars) * 3;
            cached_glitch_pool[i][0] = GLOBAL_CHINESE_DICT[pick];
            cached_glitch_pool[i][1] = GLOBAL_CHINESE_DICT[pick+1];
            cached_glitch_pool[i][2] = GLOBAL_CHINESE_DICT[pick+2];
            cached_glitch_pool[i][3] = '\0';
        }
    }

    int get_utf8_char_len(char c)
    {
        unsigned char uc = (unsigned char)c;
        if ((uc & 0x80) == 0) return 1; 
        if ((uc & 0xE0) == 0xC0) return 2; 
        if ((uc & 0xF0) == 0xE0) return 3; 
        if ((uc & 0xF8) == 0xF0) return 4; 
        return 1;
    }

    int Split_To_Lines(const char* formatted_str, char lines[][128]) 
    {
        int line_idx = 0, buf_idx = 0;
        for(int i = 0; formatted_str[i] != '\0'; i++) {
            if (formatted_str[i] == '\n') {
                lines[line_idx][buf_idx] = '\0';
                line_idx++; buf_idx = 0;
            } else {
                lines[line_idx][buf_idx++] = formatted_str[i];
            }
        }
        if (buf_idx > 0) { lines[line_idx][buf_idx] = '\0'; line_idx++; }
        return line_idx;
    }

    void Engine_Chaos_Wait(SystemLang_t lang)
    {
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        
        // 【净化】：根据宏定义自动推导起始坐标
        int start_y = UI_HEADER_HEIGHT + 10; 

        while (HAL_Is_Key_Pressed()) { delay(10); yield(); }

        while (!HAL_Is_Key_Pressed())
        {
            HAL_Sprite_Clear();
            HAL_Screen_DrawHeader();
            // 【净化】：画满全屏
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1);

            if (lang == LANG_ZH)
            {
                int lines = (sh - start_y) / 16; 
                if (lines > 12) lines = 12;
                
                // 【净化】：使用边距宏
                int max_visual_width = (sw - UI_MARGIN_LEFT) / 8; 

                for (int row = 0; row < lines; row++)
                {
                    char row_buf[256];
                    int buf_idx = 0;
                    int current_w = 0;
                    
                    while (current_w < max_visual_width - 1)
                    {
                        if (random(100) < 20 && current_w <= max_visual_width - 2) {
                            int pick = random(CACHE_SIZE);
                            row_buf[buf_idx++] = cached_glitch_pool[pick][0];
                            row_buf[buf_idx++] = cached_glitch_pool[pick][1];
                            row_buf[buf_idx++] = cached_glitch_pool[pick][2];
                            current_w += 2; 
                        } else {
                            row_buf[buf_idx++] = 33 + random(94);
                            current_w += 1; 
                        }
                    }
                    row_buf[buf_idx] = '\0';
                    HAL_Screen_ShowChineseLine(10, start_y + row * 16, row_buf);
                }
            }
            else
            {
                int lines = (sh - start_y) / 16;
                if (lines > 15) lines = 15;
                int chars_per_line = (sw - 10) / 12; 

                for (int row = 0; row < lines; row++)
                {
                    char row_buf[chars_per_line + 1];
                    for (int i = 0; i < chars_per_line; i++) row_buf[i] = 33 + random(94);
                    row_buf[chars_per_line] = '\0';
                    HAL_Screen_ShowTextLine(5, start_y + row * 16, row_buf);
                }
            }

            HAL_Screen_Update();
            SYS_SOUND_GLITCH(); // 【净化】
            delay(15);
            yield();
        }
        while (HAL_Is_Key_Pressed()) { delay(10); yield(); }
    }

    void Decode_Animation_ZH(char real_lines[][128], int actual_lines, uint8_t start_y, uint8_t max_frames)
    {
        int sw = HAL_Get_Screen_Width();
        int max_screen_lines = (HAL_Get_Screen_Height() - start_y) / 16;
        if(max_screen_lines > 12) max_screen_lines = 12;
        
        int max_visual_width = (sw - UI_MARGIN_LEFT) / 8; // 【净化】
        
        uint8_t lucky[15][40];
        for(int r = 0; r < max_screen_lines; r++) {
            for(int c = 0; c < max_visual_width; c++) {
                if (random(100) < 10 && r < actual_lines) lucky[r][c] = 0; 
                else lucky[r][c] = random(max_frames / 2) + 2;
            }
        }

        for (int frame = 0; frame < max_frames; frame++)
        {
            uint8_t all_locked = 1;
            HAL_Sprite_Clear();
            HAL_Screen_DrawHeader();
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, sw, UI_HEADER_HEIGHT, 1); // 【净化】

            for (int r = 0; r < max_screen_lines; r++)
            {
                char row_buf[256];
                int buf_idx = 0;
                int current_w = 0;
                int byte_idx = 0;

                if (r < actual_lines) {
                    while (real_lines[r][byte_idx] != '\0' && current_w < max_visual_width - 1) {
                        int char_len = get_utf8_char_len(real_lines[r][byte_idx]);
                        int char_w = (char_len > 1) ? 2 : 1;
                        
                        if (frame >= lucky[r][current_w]) {
                            for(int b = 0; b < char_len; b++) row_buf[buf_idx++] = real_lines[r][byte_idx + b];
                        } else {
                            all_locked = 0;
                            if (char_w == 2) { 
                                int pick = random(CACHE_SIZE);
                                row_buf[buf_idx++] = cached_glitch_pool[pick][0];
                                row_buf[buf_idx++] = cached_glitch_pool[pick][1];
                                row_buf[buf_idx++] = cached_glitch_pool[pick][2];
                            } else {
                                row_buf[buf_idx++] = 33 + random(94);
                            }
                        }
                        current_w += char_w;
                        byte_idx += char_len;
                    }
                }

                while (current_w < max_visual_width - 1) {
                    if (frame < lucky[r][current_w]) {
                        all_locked = 0;
                        if (random(100) < 20 && current_w <= max_visual_width - 2) {
                            int pick = random(CACHE_SIZE);
                            row_buf[buf_idx++] = cached_glitch_pool[pick][0];
                            row_buf[buf_idx++] = cached_glitch_pool[pick][1];
                            row_buf[buf_idx++] = cached_glitch_pool[pick][2];
                            current_w += 2;
                        } else {
                            row_buf[buf_idx++] = 33 + random(94);
                            current_w += 1;
                        }
                    } else {
                        row_buf[buf_idx++] = ' ';
                        current_w += 1;
                    }
                }

                row_buf[buf_idx] = '\0';
                if (buf_idx > 0) {
                    HAL_Screen_ShowChineseLine(10, start_y + r * 16, row_buf);
                }
            }

            HAL_Screen_Update();
            if (!all_locked) SYS_SOUND_GLITCH(); // 【净化】
            if (all_locked) break;
            yield();
        }
    }

    void Format_Chinese_To_Grid(const char *raw_str, char *formatted_buf)
    {
        int max_width = (HAL_Get_Screen_Width() - UI_MARGIN_LEFT) / 8 - 1; // 【净化】
        int i = 0, out_idx = 0, current_width = 0; 
        while (raw_str[i] != '\0')
        {
            int char_len = get_utf8_char_len(raw_str[i]);
            int char_w = (char_len > 1) ? 2 : 1; 

            if (char_len == 1 && raw_str[i] == ' ' && current_width == 0) { i++; continue; }

            if (current_width + char_w > max_width) {
                formatted_buf[out_idx++] = '\n'; current_width = 0;
                if (char_len == 1 && raw_str[i] == ' ') { i++; continue; }
            }
            for (int b = 0; b < char_len; b++) formatted_buf[out_idx++] = raw_str[i + b];
            current_width += char_w; i += char_len;
        }
        formatted_buf[out_idx] = '\0';
    }

    void Format_English_To_Grid(const char *raw_str, char *formatted_buf)
    {
        int max_cols = (HAL_Get_Screen_Width() - 10) / 12;
        int total_src_len = strlen(raw_str);
        int fmt_len = 0, src_idx = 0;

        while (src_idx < total_src_len) {
            while (raw_str[src_idx] == ' ') src_idx++;
            if (src_idx >= total_src_len) break;

            int chunk = total_src_len - src_idx;
            if (chunk > max_cols) {
                if (raw_str[src_idx + max_cols] == ' ' || raw_str[src_idx + max_cols] == '\0') chunk = max_cols;
                else {
                    int space_idx = -1;
                    for (int i = max_cols - 1; i > 0; i--) {
                        if (raw_str[src_idx + i] == ' ') { space_idx = i; break; }
                    }
                    chunk = (space_idx != -1) ? space_idx : max_cols;
                }
            }
            for (int i = 0; i < max_cols; i++) formatted_buf[fmt_len++] = (i < chunk) ? raw_str[src_idx + i] : ' ';
            formatted_buf[fmt_len++] = '\n';
            src_idx += chunk;
        }
        formatted_buf[fmt_len] = '\0';
    }

    void Decode_Animation_EN(char real_lines[][128], int actual_lines, uint8_t start_y, uint8_t max_frames)
    {
        int max_screen_lines = (HAL_Get_Screen_Height() - start_y) / 16;
        if(max_screen_lines > 15) max_screen_lines = 15;
        int max_screen_cols = (HAL_Get_Screen_Width() - 10) / 12;

        uint8_t lucky[15][30];
        for(int r = 0; r < max_screen_lines; r++) {
            for(int c = 0; c < max_screen_cols; c++) lucky[r][c] = random(max_frames / 2) + 2;
        }

        for (int frame = 0; frame < max_frames; frame++)
        {
            uint8_t all_locked = 1;
            HAL_Sprite_Clear();
            HAL_Screen_DrawHeader();
            HAL_Draw_Line(0, UI_HEADER_HEIGHT, HAL_Get_Screen_Width(), UI_HEADER_HEIGHT, 1); // 【净化】

            for (int r = 0; r < max_screen_lines; r++)
            {
                char row_buf[64];
                int buf_idx = 0, real_col = 0;

                if (r < actual_lines) {
                    while (real_lines[r][real_col] != '\0' && real_col < max_screen_cols) {
                        if (frame >= lucky[r][real_col]) row_buf[buf_idx++] = real_lines[r][real_col];
                        else { row_buf[buf_idx++] = 33 + random(94); all_locked = 0; }
                        real_col++;
                    }
                }
                for (int c = real_col; c < max_screen_cols; c++) {
                    if (frame < lucky[r][c]) { row_buf[buf_idx++] = 33 + random(94); all_locked = 0; }
                }

                row_buf[buf_idx] = '\0';
                if (buf_idx > 0) HAL_Screen_ShowTextLine(5, start_y + r * 16, row_buf);
            }
            HAL_Screen_Update();
            if (!all_locked) SYS_SOUND_GLITCH(); // 【净化】
            if (all_locked) break;
            yield();
        }
    }

    void Scroll_Text_EN(char real_lines[][128], int total_lines, int decoded_lines, uint8_t start_y)
    {
        int bottom_y = HAL_Get_Screen_Height() - 20; 
        int current_row = decoded_lines;
        
        while (current_row < total_lines)
        {
            if (current_row == decoded_lines) delay(500);
            for (int step = 0; step < 8; step++) {
                HAL_Screen_Scroll_Up(2); HAL_Screen_Update(); yield();
            }
            HAL_Buzzer_Play_Tone(3000, 15); // 特殊提示音保留

            char single_line_array[1][128];
            strcpy(single_line_array[0], real_lines[current_row]);
            Decode_Animation_EN(single_line_array, 1, bottom_y, 10);
            current_row++;
        }
    }

    void executePrescriptSequence()
    {
        SystemLang_t current_lang = appManager.getLanguage();
        
        // 使用静态私有变量判断当前模式
        extern bool __internal_prescript_direct_mode; 
        
        if (!__internal_prescript_direct_mode) {
            Engine_Chaos_Wait(current_lang);
        }
        // 消费掉本次状态，重置为默认的正常模式
        __internal_prescript_direct_mode = false; 

        int rule_count = Get_Prescript_Count(current_lang);
        const char *rule = Get_Prescript(current_lang, random(rule_count));
        
        static char raw_prescript[512];
        static char formatted_buf[1024];
        static char lines[30][128];
        
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0';

        int actual_lines = 0;

        if (current_lang == LANG_ZH)
        {
            Format_Chinese_To_Grid(raw_prescript, formatted_buf);
            actual_lines = Split_To_Lines(formatted_buf, lines);
            Decode_Animation_ZH(lines, actual_lines, UI_HEADER_HEIGHT + 10, 22); 
        }
        else
        {
            Format_English_To_Grid(raw_prescript, formatted_buf);
            actual_lines = Split_To_Lines(formatted_buf, lines);
            int max_screen_lines = (HAL_Get_Screen_Height() - UI_HEADER_HEIGHT - 10) / 16; 
            if (max_screen_lines > 15) max_screen_lines = 15;
            
            Decode_Animation_EN(lines, actual_lines, UI_HEADER_HEIGHT + 10, 22); 
            if (actual_lines > max_screen_lines) Scroll_Text_EN(lines, actual_lines, max_screen_lines, UI_HEADER_HEIGHT + 10);
        }

        delay(100); HAL_Buzzer_Play_Tone(1500, 60);
        delay(30); HAL_Buzzer_Play_Tone(2000, 60);
        delay(30); SYS_SOUND_CONFIRM(); 
        
        // 指令接收完毕后，重新洗牌下一次的都市意志定时器
        SysAutoPush_ResetTimer();
    }

public:
    void onCreate() override 
    { 
        Init_Glitch_Pool(); 
        executePrescriptSequence(); 
    }
    
    void onLoop() override {}
    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override { 
        SYS_SOUND_NAV(); 
        // 界面内按短按刷新，默认走正常潜伏流程
        Prescript_SetMode_Normal(); 
        executePrescriptSequence(); 
    } 
    void onKeyLong() override { appManager.popApp(); }
};

// ==========================================
// 【架构升级】：真正对外暴露的接口实现
// ==========================================
bool __internal_prescript_direct_mode = false; // 严禁外部直接访问！

void Prescript_SetMode_Normal() { __internal_prescript_direct_mode = false; }
void Prescript_SetMode_Direct() { __internal_prescript_direct_mode = true; }

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;