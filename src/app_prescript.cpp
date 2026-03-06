// 文件：src/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "prescript_data.h"

class AppPrescript : public AppBase
{
private:
    // ==========================================
    // 通用底层工具
    // ==========================================
    // 判断当前 UTF-8 字符占用了几个字节
    int get_utf8_char_len(char c)
    {
        if ((c & 0x80) == 0)
            return 1; // ASCII 英文/数字
        if ((c & 0xE0) == 0xC0)
            return 2; // 拉丁文等
        if ((c & 0xF0) == 0xE0)
            return 3; // 绝大多数中文字符
        if ((c & 0xF8) == 0xF0)
            return 4; // 特殊符号/生僻字
        return 1;
    }

    void Engine_Chaos_Wait(void)
    {
        HAL_Sprite_Clear();
        HAL_Screen_DrawHeader();
        char row_buf[21];
        row_buf[20] = '\0';

        while (HAL_Is_Key_Pressed())
        {
            delay(10);
            yield();
        }

        while (!HAL_Is_Key_Pressed())
        {
            for (int row = 0; row < 13; row++)
            {
                for (int i = 0; i < 20; i++)
                    row_buf[i] = 33 + random(94);
                HAL_Screen_ShowTextLine(0, row * 16, row_buf);
            }
            HAL_Screen_Update();
            HAL_Buzzer_Random_Glitch();
            delay(15);
            yield();
        }
        while (HAL_Is_Key_Pressed())
        {
            delay(10);
            yield();
        }
    }

    // ==========================================
    // 中文专属引擎 (ZH Engine)
    // ==========================================
    // 中文安全分行函数：根据屏幕宽度自动插入 \n，绝不腰斩汉字
    void Format_Chinese_To_Grid(const char *raw_str, char *formatted_buf)
    {
        int i = 0;
        int out_idx = 0;
        int current_line_width = 0;
        int max_line_width = 24; // 屏幕宽度容量：12个汉字(24宽) 或 24个字母(24宽)

        while (raw_str[i] != '\0')
        {
            int char_len = get_utf8_char_len(raw_str[i]);
            int char_width = (char_len > 1) ? 2 : 1; // 汉字占2格宽度，字母占1格

            // 跳过行首的多余空格
            if (char_len == 1 && raw_str[i] == ' ' && current_line_width == 0)
            {
                i++;
                continue;
            }

            // 如果这一行放不下了，就换行
            if (current_line_width + char_width > max_line_width)
            {
                formatted_buf[out_idx++] = '\n';
                current_line_width = 0;
                // 换行后如果是个空格，直接吃掉，保证下一行开头对齐
                if (char_len == 1 && raw_str[i] == ' ')
                {
                    i++;
                    continue;
                }
            }

            // 将完整的 UTF-8 字符（1~4字节）安全地拷入缓冲区
            for (int b = 0; b < char_len; b++)
            {
                formatted_buf[out_idx++] = raw_str[i + b];
            }
            current_line_width += char_width;
            i += char_len;
        }
        formatted_buf[out_idx] = '\0';
    }

    // 中文渲染器：按 \n 分割并绘制，自带双缓冲清屏防重影
    void Render_Lines_ZH(const char *text, uint8_t start_y)
    {
        HAL_Sprite_Clear();
        HAL_Screen_DrawHeader();

        char line_buf[128];
        int line_idx = 0;
        int row = 0;
        int i = 0;
        while (text[i] != '\0')
        {
            if (text[i] == '\n')
            {
                line_buf[line_idx] = '\0';
                HAL_Screen_ShowChineseLine(10, start_y + (row * 20), line_buf);
                row++;
                line_idx = 0;
            }
            else
            {
                line_buf[line_idx++] = text[i];
            }
            i++;
        }
        if (line_idx > 0)
        {
            line_buf[line_idx] = '\0';
            HAL_Screen_ShowChineseLine(10, start_y + (row * 20), line_buf);
        }
        HAL_Screen_Update();
    }

    // 中文安全解码动画：识别出 3 字节汉字，并用全角符号进行替换特效
    void Decode_Animation_ZH(const char *target_str, uint8_t start_y, uint8_t max_frames)
    {
        int total_bytes = strlen(target_str);
        char display_buf[1024];

        // 提取逻辑字符的边界
        int char_starts[300];
        int char_lens[300];
        int char_count = 0;
        int idx = 0;
        while (idx < total_bytes && char_count < 300)
        {
            int len = get_utf8_char_len(target_str[idx]);
            char_starts[char_count] = idx;
            char_lens[char_count] = len;
            char_count++;
            idx += len;
        }

        // 分配随机解锁帧数
        uint8_t lucky_jumps[300];
        for (int i = 0; i < char_count; i++)
        {
            lucky_jumps[i] = random(max_frames / 2) + 2;
        }

        // 【中文酷炫特效】：准备全角的乱码占位符
        const char *glitch_chars[] = {"＊", "？", "！", "＃", "＠", "＆", "％"};

        for (int frame = 0; frame < max_frames; frame++)
        {
            uint8_t all_locked = 1;
            int buf_idx = 0;

            for (int i = 0; i < char_count; i++)
            {
                int src_idx = char_starts[i];
                int len = char_lens[i];
                char first_byte = target_str[src_idx];

                // 如果已经解锁，或者是空格、换行、下划线，直接显示真容
                if (frame >= lucky_jumps[i] || first_byte == ' ' || first_byte == '\n' || first_byte == '_')
                {
                    for (int b = 0; b < len; b++)
                        display_buf[buf_idx++] = target_str[src_idx + b];
                }
                else
                {
                    all_locked = 0;
                    if (len == 1)
                    { // 英文乱码特效
                        display_buf[buf_idx++] = 33 + random(94);
                    }
                    else if (len == 3)
                    { // 汉字乱码特效：随机抽一个全角符号塞进去，完美维持排版宽度！
                        const char *selected_glitch = glitch_chars[random(7)];
                        display_buf[buf_idx++] = selected_glitch[0];
                        display_buf[buf_idx++] = selected_glitch[1];
                        display_buf[buf_idx++] = selected_glitch[2];
                    }
                    else
                    { // 罕见字节长度，不作处理防报错
                        for (int b = 0; b < len; b++)
                            display_buf[buf_idx++] = target_str[src_idx + b];
                    }
                }
            }
            display_buf[buf_idx] = '\0';

            Render_Lines_ZH(display_buf, start_y);

            if (!all_locked)
                HAL_Buzzer_Random_Glitch();
            if (all_locked)
                break;
            yield();
        }
    }

    // ==========================================
    // 英文专属引擎 (EN Engine) - 完美保留原汁原味
    // ==========================================
    void Format_English_To_Grid(const char *raw_str, char *formatted_buf, int *out_len)
    {
        int total_src_len = strlen(raw_str);
        int fmt_len = 0;
        int src_idx = 0;

        while (src_idx < total_src_len)
        {
            while (raw_str[src_idx] == ' ')
                src_idx++;
            if (src_idx >= total_src_len)
                break;

            int chunk = total_src_len - src_idx;
            if (chunk > 20)
            {
                if (raw_str[src_idx + 20] == ' ' || raw_str[src_idx + 20] == '\0')
                {
                    chunk = 20;
                }
                else
                {
                    int space_idx = -1;
                    for (int i = 19; i > 0; i--)
                    {
                        if (raw_str[src_idx + i] == ' ')
                        {
                            space_idx = i;
                            break;
                        }
                    }
                    chunk = (space_idx != -1) ? space_idx : 20;
                }
            }
            for (int i = 0; i < 20; i++)
            {
                formatted_buf[fmt_len++] = (i < chunk) ? raw_str[src_idx + i] : ' ';
            }
            src_idx += chunk;
        }
        formatted_buf[fmt_len] = '\0';
        *out_len = fmt_len;
    }

    void Render_Lines_EN(char *buf, uint16_t len, uint8_t start_y)
    {
        HAL_Sprite_Clear();
        HAL_Screen_DrawHeader();
        char line_buf[21];
        int lines = len / 20;
        for (int row = 0; row < lines; row++)
        {
            strncpy(line_buf, &buf[row * 20], 20);
            line_buf[20] = '\0';
            HAL_Screen_ShowTextLine(0, start_y + (row * 16), line_buf);
        }
        HAL_Screen_Update();
    }

    void Decode_Animation_EN(const char *target_str, uint16_t len, uint8_t start_y, uint8_t max_frames)
    {
        char display_buf[265];
        uint8_t lucky_jumps[265];

        for (int i = 0; i < len; i++)
        {
            lucky_jumps[i] = random(max_frames / 2) + 2;
        }
        display_buf[len] = '\0';

        for (int frame = 0; frame < max_frames; frame++)
        {
            uint8_t all_locked = 1;
            for (int i = 0; i < len; i++)
            {
                if (frame >= lucky_jumps[i] || target_str[i] == ' ')
                {
                    display_buf[i] = target_str[i];
                }
                else
                {
                    display_buf[i] = 33 + random(94);
                    all_locked = 0;
                }
            }
            Render_Lines_EN(display_buf, len, start_y);
            if (!all_locked)
                HAL_Buzzer_Random_Glitch();
            if (all_locked)
                break;
            yield();
        }
    }

    void Scroll_Text_EN(const char *formatted_buf, int fmt_len)
    {
        if (fmt_len <= 260)
            return;
        int current_idx = 260;
        while (current_idx < fmt_len)
        {
            if (current_idx == 260)
                delay(500);
            for (int step = 0; step < 8; step++)
            {
                HAL_Screen_Scroll_Up(2);
                HAL_Screen_Update();
                yield();
            }
            HAL_Buzzer_Play_Tone(3000, 15);

            char line_buf[21];
            strncpy(line_buf, &formatted_buf[current_idx], 20);
            line_buf[20] = '\0';

            Decode_Animation_EN(line_buf, 20, 192, 10);
            current_idx += 20;
        }
    }

    // ==========================================
    // 核心流转：智能分流中英文
    // ==========================================
    void executePrescriptSequence()
    {
        Engine_Chaos_Wait();

        SystemLang_t current_lang = appManager.getLanguage();
        int rule_count = Get_Prescript_Count(current_lang);
        const char *rule = Get_Prescript(current_lang, random(rule_count));

        char raw_prescript[512];
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0';

        // --- 双语分流处理 ---
        if (current_lang == LANG_ZH)
        {
            char formatted_buf[1024];
            // 1. 中文安全排版（按实际宽度，填入 \n）
            Format_Chinese_To_Grid(raw_prescript, formatted_buf);

            // 2. 启动中文专属乱码特效（全角字符解码）
            // 注意：start_y 设为 30，避开顶部的 Header
            Decode_Animation_ZH(formatted_buf, 30, 22);

            // 中文目前短文本不开启滚动，直接展示即可
        }
        else
        {
            char formatted_buf[1024];
            int fmt_len = 0;
            // 原汁原味的英文排版
            Format_English_To_Grid(raw_prescript, formatted_buf, &fmt_len);

            char first_screen_buf[265];
            int first_chunk = (fmt_len > 260) ? 260 : fmt_len;
            strncpy(first_screen_buf, formatted_buf, first_chunk);
            for (int i = first_chunk; i < 260; i++)
                first_screen_buf[i] = ' ';
            first_screen_buf[260] = '\0';

            Decode_Animation_EN(first_screen_buf, 260, 26, 22);
            Scroll_Text_EN(formatted_buf, fmt_len);
        }

        delay(100);
        HAL_Buzzer_Play_Tone(1500, 60);
        delay(30);
        HAL_Buzzer_Play_Tone(2000, 60);
        delay(30);
        HAL_Buzzer_Play_Tone(2500, 150);
    }

public:
    void onCreate() override
    {
        executePrescriptSequence();
    }

    void onLoop() override {}
    void onDestroy() override {}
    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        HAL_Buzzer_Play_Tone(2200, 40);
        executePrescriptSequence();
    }

// 找到这一行，改成 popApp！
    void onKeyLong() override {
        appManager.popApp(); // 【修改】：看完了原路返回主菜单
    }
};

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;