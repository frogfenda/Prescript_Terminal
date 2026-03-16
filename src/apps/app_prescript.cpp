// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_fs.h" // <--- 引入全新的硬盘系统
#include "sys_auto_push.h"
#include "sys_config.h"

class AppPrescript : public AppBase
{
private:
    static const int MAX_DATA_LINES = 20;
    static const int UI_CH_START_Y = 6;
    static const int UI_CH_ROW_HEIGHT = 16;
    static const int UI_CH_MAX_LINES = 4;
    static const int UI_CH_MARGIN_X = 6;
    static const int UI_CH_MAX_COLS = 45;

    static const int UI_EN_START_Y = 6;
    static const int UI_EN_ROW_HEIGHT = 14;
    static const int UI_EN_MAX_LINES = 5;
    static const int UI_EN_MARGIN_X = 4;
    static const int UI_EN_MAX_COLS = 46;

    static const int ANIM_CHAOS_DELAY = 15;
    static const int ANIM1_FRAMES = 22;
    static const int ANIM1_DELAY = 40;
    static const int ANIM2_SPEED_DELAY = 30;
    static const int ANIM3_GLITCH_COUNT = 4;
    static const int ANIM3_GLITCH_DELAY = 20;
    static const int ANIM4_BASE_FRAMES = 6;
    static const int ANIM4_DELAY = 20;
    static const int AUDIO_DONE_DELAY = 100;

    const char *GLOBAL_CHINESE_DICT = "数据错误系统异常致命警告内存溢出未知指令核心损坏参数丢失连接中断身份拒绝权限不足矩阵终端序列重载覆写剥离重构解析编译代码节点网关漏洞端口渗透代理溯源封锁拦截劫持";

    static const int CACHE_SIZE = 300;
    char cached_glitch_pool[CACHE_SIZE][4];

    char m_lines[MAX_DATA_LINES][256];
    int m_actual_lines = 0;
    int m_scroll_offset = 0;
    // 【新增 1】：PSRAM 内存指针
    uint8_t *wav_procedure_data = nullptr;
    uint32_t wav_procedure_len = 0;

    uint8_t *wav_final_data = nullptr;
    uint32_t wav_final_len = 0;
    // 【新增 2】：专属的解码过程发声器（如果有 WAV 就播 WAV，没有就降级回电子音）
    enum State
    {
        S_INIT,
        S_WAIT_RELEASE,
        S_CHAOS,
        S_DECODE,
        S_DONE
    };
    State m_state;
    void playProcedureSound()
    {
        if (wav_procedure_data)
        {
            HAL_Play_Real_Sound(wav_procedure_data, wav_procedure_len);
        }
        else
        {
            SYS_SOUND_GLITCH();
        }
    }

    void Init_Glitch_Pool()
    {
        int total_chars = strlen(GLOBAL_CHINESE_DICT) / 3;
        for (int i = 0; i < CACHE_SIZE; i++)
        {
            int pick = random(total_chars) * 3;
            cached_glitch_pool[i][0] = GLOBAL_CHINESE_DICT[pick];
            cached_glitch_pool[i][1] = GLOBAL_CHINESE_DICT[pick + 1];
            cached_glitch_pool[i][2] = GLOBAL_CHINESE_DICT[pick + 2];
            cached_glitch_pool[i][3] = '\0';
        }
    }

    int get_utf8_char_len(char c)
    {
        unsigned char uc = (unsigned char)c;
        if ((uc & 0x80) == 0)
            return 1;
        if ((uc & 0xE0) == 0xC0)
            return 2;
        if ((uc & 0xF0) == 0xE0)
            return 3;
        if ((uc & 0xF8) == 0xF0)
            return 4;
        return 1;
    }

    int Split_To_Lines(const char *formatted_str, char lines[][256])
    {
        int line_idx = 0, buf_idx = 0;
        for (int i = 0; formatted_str[i] != '\0'; i++)
        {
            if (formatted_str[i] == '\n')
            {
                lines[line_idx][buf_idx] = '\0';
                line_idx++;
                buf_idx = 0;
                if (line_idx >= MAX_DATA_LINES)
                    break;
            }
            else
            {
                if (buf_idx < 255)
                    lines[line_idx][buf_idx++] = formatted_str[i];
            }
        }
        if (buf_idx > 0 && line_idx < MAX_DATA_LINES)
        {
            lines[line_idx][buf_idx] = '\0';
            line_idx++;
        }
        return line_idx;
    }

    void Format_Chinese_To_Grid(const char *raw_str, char *formatted_buf)
    {
        int max_v = UI_CH_MAX_COLS;
        int current_w = 0;
        int buf_idx = 0;
        for (int i = 0; raw_str[i] != '\0';)
        {
            if (raw_str[i] == '\n')
            {
                formatted_buf[buf_idx++] = raw_str[i++];
                current_w = 0;
                continue;
            }
            int clen = get_utf8_char_len(raw_str[i]);
            int cw = (clen > 1) ? 2 : 1;
            if (current_w + cw > max_v)
            {
                formatted_buf[buf_idx++] = '\n';
                current_w = 0;
            }
            for (int b = 0; b < clen; b++)
                formatted_buf[buf_idx++] = raw_str[i++];
            current_w += cw;
        }
        formatted_buf[buf_idx] = '\0';
    }

    void Format_English_To_Grid(const char *raw_str, char *formatted_buf)
    {
        int max_w = UI_EN_MAX_COLS;
        int current_w = 0;
        int buf_idx = 0;
        int i = 0;

        while (raw_str[i] != '\0')
        {
            if (raw_str[i] == '\n')
            {
                formatted_buf[buf_idx++] = '\n';
                current_w = 0;
                i++;
                continue;
            }
            if (current_w == 0 && raw_str[i] == ' ')
            {
                i++;
                continue;
            }

            int scan_i = i;
            int w_len = 0;
            while (raw_str[scan_i] != '\0' && raw_str[scan_i] != ' ' && raw_str[scan_i] != '\n')
            {
                w_len += (get_utf8_char_len(raw_str[scan_i]) > 1) ? 2 : 1;
                scan_i += get_utf8_char_len(raw_str[scan_i]);
            }

            if (current_w > 0 && current_w + w_len > max_w)
            {
                formatted_buf[buf_idx++] = '\n';
                current_w = 0;
                continue;
            }

            while (i < scan_i)
            {
                int clen = get_utf8_char_len(raw_str[i]);
                int cw = (clen > 1) ? 2 : 1;
                if (current_w + cw > max_w)
                {
                    formatted_buf[buf_idx++] = '\n';
                    current_w = 0;
                }
                for (int b = 0; b < clen; b++)
                    formatted_buf[buf_idx++] = raw_str[i++];
                current_w += cw;
            }
            if (raw_str[i] == ' ')
            {
                if (current_w < max_w)
                {
                    formatted_buf[buf_idx++] = ' ';
                    current_w++;
                }
                i++;
            }
        }
        formatted_buf[buf_idx] = '\0';
    }

    void drawChaosFrame(SystemLang_t lang)
    {
        HAL_Sprite_Clear();
        if (lang == LANG_ZH)
        {
            for (int row = 0; row < UI_CH_MAX_LINES; row++)
            {
                char row_buf[256];
                int buf_idx = 0, current_w = 0;
                while (current_w < UI_CH_MAX_COLS - 1)
                {
                    if (random(100) < 40 && current_w <= UI_CH_MAX_COLS - 2)
                    {
                        int pick = random(CACHE_SIZE);
                        row_buf[buf_idx++] = cached_glitch_pool[pick][0];
                        row_buf[buf_idx++] = cached_glitch_pool[pick][1];
                        row_buf[buf_idx++] = cached_glitch_pool[pick][2];
                        current_w += 2;
                    }
                    else
                    {
                        row_buf[buf_idx++] = 33 + random(94);
                        current_w += 1;
                    }
                }
                row_buf[buf_idx] = '\0';
                HAL_Screen_ShowChineseLine(UI_CH_MARGIN_X, UI_CH_START_Y + row * UI_CH_ROW_HEIGHT, row_buf);
            }
        }
        else
        {
            for (int row = 0; row < UI_EN_MAX_LINES; row++)
            {
                char row_buf[UI_EN_MAX_COLS + 1];
                for (int i = 0; i < UI_EN_MAX_COLS; i++)
                    row_buf[i] = 33 + random(94);
                row_buf[UI_EN_MAX_COLS] = '\0';
                HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + row * UI_EN_ROW_HEIGHT, row_buf);
            }
        }
        HAL_Screen_Update();
        SYS_SOUND_GLITCH();
    }

    void drawDoneFrame()
    {
        HAL_Sprite_Clear();
        SystemLang_t current_lang = appManager.getLanguage();

        int max_vis = (current_lang == LANG_ZH) ? UI_CH_MAX_LINES : UI_EN_MAX_LINES;
        int start_y = (current_lang == LANG_ZH) ? UI_CH_START_Y : UI_EN_START_Y;
        int row_h = (current_lang == LANG_ZH) ? UI_CH_ROW_HEIGHT : UI_EN_ROW_HEIGHT;
        int margin_x = (current_lang == LANG_ZH) ? UI_CH_MARGIN_X : UI_EN_MARGIN_X;

        int draw_count = (m_actual_lines - m_scroll_offset > max_vis) ? max_vis : (m_actual_lines - m_scroll_offset);

        for (int i = 0; i < draw_count; i++)
        {
            int r = m_scroll_offset + i;
            if (current_lang == LANG_ZH)
            {
                HAL_Screen_ShowChineseLine(margin_x, start_y + i * row_h, m_lines[r]);
            }
            else
            {
                HAL_Screen_ShowTextLine(margin_x, start_y + i * row_h, m_lines[r]);
            }
        }

        if (m_actual_lines > max_vis)
        {
            int max_offset = m_actual_lines - max_vis;
            int track_h = max_vis * row_h;
            int bar_h = track_h * max_vis / m_actual_lines;
            if (bar_h < 4)
                bar_h = 4;
            int bar_y = start_y + (track_h - bar_h) * m_scroll_offset / max_offset;
            HAL_Fill_Rect(280, bar_y, 2, bar_h, 1);
        }
        HAL_Screen_Update();
    }

    void executeDecodeSequence()
    {
        SystemLang_t current_lang = appManager.getLanguage();
        extern int __internal_prescript_mode;
        extern char __internal_custom_prescript[512];
        const char *rule;
        static String fs_rule_str; // 用静态变量接住 String，保证 c_str() 指针不被销毁

        if (__internal_prescript_mode == 3 || __internal_prescript_mode == 4)
        {
            rule = __internal_custom_prescript;
        }
        else
        {
            if (__internal_prescript_mode == 3 || __internal_prescript_mode == 4)
            {
                // 这是系统推送或者闹钟强行插入的单条指令
                rule = __internal_custom_prescript;
            }
            else
            {
                // 正常抽取：直接从底层双语硬盘内存池中随机抽取！没有任何概率分流！
                if (current_lang == LANG_ZH)
                {
                    int sz = sys_prescripts_zh.size();
                    if (sz > 0)
                        fs_rule_str = sys_prescripts_zh[random(sz)];
                    else
                        fs_rule_str = "错误：中文指令库 prescripts_zh.txt 为空。";
                }
                else
                {
                    int sz = sys_prescripts_en.size();
                    if (sz > 0)
                        fs_rule_str = sys_prescripts_en[random(sz)];
                    else
                        fs_rule_str = "ERR: prescripts_en.txt EMPTY.";
                }
                rule = fs_rule_str.c_str();
            }
        }

        static char raw_prescript[1024];
        static char formatted_buf[2048];
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0';

        if (current_lang == LANG_ZH)
        {
            Format_Chinese_To_Grid(raw_prescript, formatted_buf);
            m_actual_lines = Split_To_Lines(formatted_buf, m_lines);
            int draw_lines = (m_actual_lines > UI_CH_MAX_LINES) ? UI_CH_MAX_LINES : m_actual_lines;

            if (sysConfig.decode_anim_style == 1)
            {
                int total_chars = 0;
                for (int r = 0; r < draw_lines; r++)
                {
                    int i = 0;
                    while (m_lines[r][i] != '\0')
                    {
                        total_chars++;
                        i += get_utf8_char_len(m_lines[r][i]);
                    }
                }
                for (int frame = 0; frame <= total_chars; frame++)
                {
                    HAL_Sprite_Clear();
                    int current_char = 0;
                    for (int r = 0; r < draw_lines; r++)
                    {
                        char print_buf[256] = {0};
                        int buf_idx = 0;
                        int i = 0;
                        while (m_lines[r][i] != '\0')
                        {
                            int clen = get_utf8_char_len(m_lines[r][i]);
                            if (current_char < frame)
                            {
                                for (int b = 0; b < clen; b++)
                                    print_buf[buf_idx++] = m_lines[r][i + b];
                            }
                            else if (current_char == frame)
                            {
                                print_buf[buf_idx] = '\0';
                                HAL_Screen_ShowChineseLine(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf);
                                int cursor_x = UI_CH_MARGIN_X + HAL_Get_Text_Width(print_buf);
                                HAL_Fill_Rect(cursor_x, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT + 2, clen > 1 ? 12 : 6, 12, 1);
                            }
                            current_char++;
                            i += clen;
                        }
                        if (buf_idx > 0 && current_char <= frame)
                        {
                            print_buf[buf_idx] = '\0';
                            HAL_Screen_ShowChineseLine(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf);
                        }
                    }
                    HAL_Screen_Update();
                    if (frame < total_chars)
                    {
                        playProcedureSound();
                        delay(ANIM2_SPEED_DELAY);
                    }
                }
            }
            else if (sysConfig.decode_anim_style == 2)
            {
                int total_chars = 0;
                for (int r = 0; r < draw_lines; r++)
                {
                    int i = 0;
                    while (m_lines[r][i] != '\0')
                    {
                        total_chars++;
                        i += get_utf8_char_len(m_lines[r][i]);
                    }
                }
                for (int target = 0; target <= total_chars; target++)
                {
                    bool is_space = false;
                    if (target < total_chars)
                    {
                        int c_count = 0;
                        for (int r = 0; r < draw_lines; r++)
                        {
                            int i = 0;
                            while (m_lines[r][i] != '\0')
                            {
                                if (c_count == target)
                                {
                                    if (m_lines[r][i] == ' ')
                                        is_space = true;
                                    break;
                                }
                                c_count++;
                                i += get_utf8_char_len(m_lines[r][i]);
                            }
                            if (c_count == target && is_space)
                                break;
                        }
                    }
                    int frames = (is_space || target == total_chars) ? 1 : ANIM3_GLITCH_COUNT;
                    for (int f = 0; f < frames; f++)
                    {
                        HAL_Sprite_Clear();
                        int current_char = 0;
                        for (int r = 0; r < draw_lines; r++)
                        {
                            char print_buf[256] = {0};
                            int buf_idx = 0;
                            int i = 0;
                            while (m_lines[r][i] != '\0')
                            {
                                int clen = get_utf8_char_len(m_lines[r][i]);
                                if (current_char < target)
                                {
                                    for (int b = 0; b < clen; b++)
                                        print_buf[buf_idx++] = m_lines[r][i + b];
                                }
                                else if (current_char == target && target < total_chars)
                                {
                                    if (is_space)
                                    {
                                        print_buf[buf_idx++] = ' ';
                                    }
                                    else
                                    {
                                        if (clen > 1)
                                        {
                                            if (random(100) < 40)
                                            {
                                                int p = random(CACHE_SIZE);
                                                print_buf[buf_idx++] = cached_glitch_pool[p][0];
                                                print_buf[buf_idx++] = cached_glitch_pool[p][1];
                                                print_buf[buf_idx++] = cached_glitch_pool[p][2];
                                            }
                                            else
                                            {
                                                print_buf[buf_idx++] = 33 + random(94);
                                                print_buf[buf_idx++] = 33 + random(94);
                                            }
                                        }
                                        else
                                        {
                                            print_buf[buf_idx++] = 33 + random(94);
                                        }
                                    }
                                }
                                current_char++;
                                i += clen;
                            }
                            if (buf_idx > 0)
                            {
                                print_buf[buf_idx] = '\0';
                                HAL_Screen_ShowChineseLine(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf);
                            }
                        }
                        HAL_Screen_Update();
                        if (target < total_chars && !is_space)
                        {
                            playProcedureSound();
                            delay(ANIM3_GLITCH_DELAY);
                        }
                    }
                }
            }
            else if (sysConfig.decode_anim_style == 3)
            {
                int total_chars = 0;
                for (int r = 0; r < draw_lines; r++)
                {
                    int i = 0;
                    while (m_lines[r][i] != '\0')
                    {
                        total_chars++;
                        i += get_utf8_char_len(m_lines[r][i]);
                    }
                }
                int total_frames = total_chars + ANIM4_BASE_FRAMES;
                for (int frame = 0; frame <= total_frames; frame++)
                {
                    HAL_Sprite_Clear();
                    int current_char = 0;
                    for (int r = 0; r < draw_lines; r++)
                    {
                        char print_buf[256] = {0};
                        int buf_idx = 0;
                        int i = 0;
                        while (m_lines[r][i] != '\0')
                        {
                            int clen = get_utf8_char_len(m_lines[r][i]);
                            int resolve_frame = current_char + ANIM4_BASE_FRAMES;
                            if (frame >= resolve_frame)
                            {
                                for (int b = 0; b < clen; b++)
                                    print_buf[buf_idx++] = m_lines[r][i + b];
                            }
                            else
                            {
                                if (m_lines[r][i] == ' ')
                                {
                                    print_buf[buf_idx++] = ' ';
                                }
                                else
                                {
                                    if (clen > 1)
                                    {
                                        if (random(100) < 40)
                                        {
                                            int p = random(CACHE_SIZE);
                                            print_buf[buf_idx++] = cached_glitch_pool[p][0];
                                            print_buf[buf_idx++] = cached_glitch_pool[p][1];
                                            print_buf[buf_idx++] = cached_glitch_pool[p][2];
                                        }
                                        else
                                        {
                                            print_buf[buf_idx++] = 33 + random(94);
                                            print_buf[buf_idx++] = 33 + random(94);
                                        }
                                    }
                                    else
                                    {
                                        print_buf[buf_idx++] = 33 + random(94);
                                    }
                                }
                            }
                            current_char++;
                            i += clen;
                        }
                        if (buf_idx > 0)
                        {
                            print_buf[buf_idx] = '\0';
                            HAL_Screen_ShowChineseLine(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf);
                        }
                    }
                    HAL_Screen_Update();
                    if (frame < total_frames)
                    {
                        playProcedureSound();
                        delay(ANIM4_DELAY);
                    }
                }
            }
            else
            {
                uint8_t lucky[UI_CH_MAX_LINES][60];
                for (int r = 0; r < UI_CH_MAX_LINES; r++)
                    for (int c = 0; c < UI_CH_MAX_COLS; c++)
                        lucky[r][c] = random(11) + 2;
                for (int frame = 0; frame < ANIM1_FRAMES; frame++)
                {
                    uint8_t all_locked = 1;
                    HAL_Sprite_Clear();
                    for (int r = 0; r < UI_CH_MAX_LINES; r++)
                    {
                        char row_buf[512];
                        int buf_idx = 0, current_w = 0, byte_idx = 0;
                        if (r < draw_lines)
                        {
                            while (m_lines[r][byte_idx] != '\0' && current_w < UI_CH_MAX_COLS - 1)
                            {
                                int clen = get_utf8_char_len(m_lines[r][byte_idx]);
                                int cw = (clen > 1) ? 2 : 1;
                                if (frame >= lucky[r][current_w])
                                {
                                    for (int b = 0; b < clen; b++)
                                        row_buf[buf_idx++] = m_lines[r][byte_idx + b];
                                }
                                else
                                {
                                    all_locked = 0;
                                    if (cw == 2)
                                    {
                                        if (random(100) < 40)
                                        {
                                            int p = random(CACHE_SIZE);
                                            row_buf[buf_idx++] = cached_glitch_pool[p][0];
                                            row_buf[buf_idx++] = cached_glitch_pool[p][1];
                                            row_buf[buf_idx++] = cached_glitch_pool[p][2];
                                        }
                                        else
                                        {
                                            row_buf[buf_idx++] = 33 + random(94);
                                            row_buf[buf_idx++] = 33 + random(94);
                                        }
                                    }
                                    else
                                    {
                                        row_buf[buf_idx++] = 33 + random(94);
                                    }
                                }
                                current_w += cw;
                                byte_idx += clen;
                            }
                        }
                        while (current_w < UI_CH_MAX_COLS - 1)
                        {
                            if (frame < lucky[r][current_w])
                            {
                                all_locked = 0;
                                if (random(100) < 40 && current_w <= UI_CH_MAX_COLS - 2)
                                {
                                    int p = random(CACHE_SIZE);
                                    row_buf[buf_idx++] = cached_glitch_pool[p][0];
                                    row_buf[buf_idx++] = cached_glitch_pool[p][1];
                                    row_buf[buf_idx++] = cached_glitch_pool[p][2];
                                    current_w += 2;
                                }
                                else
                                {
                                    row_buf[buf_idx++] = 33 + random(94);
                                    current_w += 1;
                                }
                            }
                            else
                            {
                                row_buf[buf_idx++] = ' ';
                                current_w += 1;
                            }
                        }
                        row_buf[buf_idx] = '\0';
                        if (buf_idx > 0)
                            HAL_Screen_ShowChineseLine(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, row_buf);
                    }
                    HAL_Screen_Update();
                    if (!all_locked)
                        playProcedureSound();
                    if (all_locked)
                        break;
                    delay(ANIM1_DELAY);
                    yield();
                }
            }
        }
        else
        {
            // === 英文解析保持不变 ===
            Format_English_To_Grid(raw_prescript, formatted_buf);
            m_actual_lines = Split_To_Lines(formatted_buf, m_lines);
            int draw_lines = (m_actual_lines > UI_EN_MAX_LINES) ? UI_EN_MAX_LINES : m_actual_lines;

            if (sysConfig.decode_anim_style == 1)
            {
                int total_chars = 0;
                for (int r = 0; r < draw_lines; r++)
                {
                    int i = 0;
                    while (m_lines[r][i] != '\0')
                    {
                        total_chars++;
                        i++;
                    }
                }
                for (int frame = 0; frame <= total_chars; frame++)
                {
                    HAL_Sprite_Clear();
                    int current_char = 0;
                    for (int r = 0; r < draw_lines; r++)
                    {
                        char print_buf[256] = {0};
                        int buf_idx = 0;
                        int i = 0;
                        while (m_lines[r][i] != '\0')
                        {
                            if (current_char < frame)
                            {
                                print_buf[buf_idx++] = m_lines[r][i];
                            }
                            else if (current_char == frame)
                            {
                                print_buf[buf_idx] = '\0';
                                HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, print_buf);
                                int cursor_x = UI_EN_MARGIN_X + buf_idx * 6;
                                HAL_Fill_Rect(cursor_x, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, 6, 12, 1);
                            }
                            current_char++;
                            i++;
                        }
                        if (buf_idx > 0 && current_char <= frame)
                        {
                            print_buf[buf_idx] = '\0';
                            HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, print_buf);
                        }
                    }
                    HAL_Screen_Update();
                    if (frame < total_chars)
                    {
                        playProcedureSound();
                        delay(ANIM2_SPEED_DELAY);
                    }
                }
            }
            else if (sysConfig.decode_anim_style == 2)
            {
                int total_chars = 0;
                for (int r = 0; r < draw_lines; r++)
                {
                    int i = 0;
                    while (m_lines[r][i] != '\0')
                    {
                        total_chars++;
                        i++;
                    }
                }
                for (int target = 0; target <= total_chars; target++)
                {
                    bool is_space = false;
                    if (target < total_chars)
                    {
                        int c_count = 0;
                        for (int r = 0; r < draw_lines; r++)
                        {
                            int i = 0;
                            while (m_lines[r][i] != '\0')
                            {
                                if (c_count == target)
                                {
                                    if (m_lines[r][i] == ' ')
                                        is_space = true;
                                    break;
                                }
                                c_count++;
                                i++;
                            }
                            if (c_count == target && is_space)
                                break;
                        }
                    }
                    int frames = (is_space || target == total_chars) ? 1 : ANIM3_GLITCH_COUNT;
                    for (int f = 0; f < frames; f++)
                    {
                        HAL_Sprite_Clear();
                        int current_char = 0;
                        for (int r = 0; r < draw_lines; r++)
                        {
                            char print_buf[256] = {0};
                            int buf_idx = 0;
                            int i = 0;
                            while (m_lines[r][i] != '\0')
                            {
                                if (current_char < target)
                                {
                                    print_buf[buf_idx++] = m_lines[r][i];
                                }
                                else if (current_char == target && target < total_chars)
                                {
                                    if (is_space)
                                        print_buf[buf_idx++] = ' ';
                                    else
                                        print_buf[buf_idx++] = 33 + random(94);
                                }
                                current_char++;
                                i++;
                            }
                            if (buf_idx > 0)
                            {
                                print_buf[buf_idx] = '\0';
                                HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, print_buf);
                            }
                        }
                        HAL_Screen_Update();
                        if (target < total_chars && !is_space)
                        {
                            playProcedureSound();
                            delay(ANIM3_GLITCH_DELAY);
                        }
                    }
                }
            }
            else if (sysConfig.decode_anim_style == 3)
            {
                int total_chars = 0;
                for (int r = 0; r < draw_lines; r++)
                {
                    int i = 0;
                    while (m_lines[r][i] != '\0')
                    {
                        total_chars++;
                        i++;
                    }
                }
                int total_frames = total_chars + ANIM4_BASE_FRAMES;
                for (int frame = 0; frame <= total_frames; frame++)
                {
                    HAL_Sprite_Clear();
                    int current_char = 0;
                    for (int r = 0; r < draw_lines; r++)
                    {
                        char print_buf[256] = {0};
                        int buf_idx = 0;
                        int i = 0;
                        while (m_lines[r][i] != '\0')
                        {
                            int resolve_frame = current_char + ANIM4_BASE_FRAMES;
                            if (frame >= resolve_frame)
                            {
                                print_buf[buf_idx++] = m_lines[r][i];
                            }
                            else
                            {
                                if (m_lines[r][i] == ' ')
                                    print_buf[buf_idx++] = ' ';
                                else
                                    print_buf[buf_idx++] = 33 + random(94);
                            }
                            current_char++;
                            i++;
                        }
                        if (buf_idx > 0)
                        {
                            print_buf[buf_idx] = '\0';
                            HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, print_buf);
                        }
                    }
                    HAL_Screen_Update();
                    if (frame < total_frames)
                    {
                        playProcedureSound();
                        delay(ANIM4_DELAY);
                    }
                }
            }
            else
            {
                uint8_t lucky[UI_EN_MAX_LINES][60];
                for (int r = 0; r < UI_EN_MAX_LINES; r++)
                    for (int c = 0; c < UI_EN_MAX_COLS; c++)
                        lucky[r][c] = random(11) + 2;
                for (int frame = 0; frame < ANIM1_FRAMES; frame++)
                {
                    uint8_t all_locked = 1;
                    HAL_Sprite_Clear();
                    for (int r = 0; r < UI_EN_MAX_LINES; r++)
                    {
                        char row_buf[512];
                        int buf_idx = 0, current_w = 0, byte_idx = 0;
                        if (r < draw_lines)
                        {
                            while (m_lines[r][byte_idx] != '\0' && current_w < UI_EN_MAX_COLS)
                            {
                                int clen = get_utf8_char_len(m_lines[r][byte_idx]);
                                int cw = (clen > 1) ? 2 : 1;
                                if (frame >= lucky[r][current_w])
                                {
                                    for (int b = 0; b < clen; b++)
                                        row_buf[buf_idx++] = m_lines[r][byte_idx + b];
                                }
                                else
                                {
                                    all_locked = 0;
                                    if (cw == 2)
                                    {
                                        int p = random(CACHE_SIZE);
                                        row_buf[buf_idx++] = cached_glitch_pool[p][0];
                                        row_buf[buf_idx++] = cached_glitch_pool[p][1];
                                        row_buf[buf_idx++] = cached_glitch_pool[p][2];
                                    }
                                    else
                                        row_buf[buf_idx++] = 33 + random(94);
                                }
                                current_w += cw;
                                byte_idx += clen;
                            }
                        }
                        while (current_w < UI_EN_MAX_COLS)
                        {
                            if (frame < lucky[r][current_w])
                            {
                                all_locked = 0;
                                row_buf[buf_idx++] = 33 + random(94);
                            }
                            else
                            {
                                row_buf[buf_idx++] = ' ';
                            }
                            current_w++;
                        }
                        row_buf[buf_idx] = '\0';
                        if (buf_idx > 0)
                            HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, row_buf);
                    }
                    HAL_Screen_Update();
                    if (!all_locked)
                        playProcedureSound();
                    if (all_locked)
                        break;
                    delay(ANIM1_DELAY);
                    yield();
                }
            }
        }

        m_scroll_offset = 0;
        drawDoneFrame();

        // 【新增 5】：替换解码成功的提示音
        if (wav_final_data)
        {
            // 这瞬间会利用刚刚写的打断机制，强行截断 procedure 的余音
            HAL_Play_Real_Sound(wav_final_data, wav_final_len);
        }
        else
        {
            delay(AUDIO_DONE_DELAY);
            HAL_Buzzer_Play_Tone(1500, 60);
            delay(30);
            HAL_Buzzer_Play_Tone(2000, 60);
            delay(30);
            SYS_SOUND_CONFIRM();
        }

        SysAutoPush_ResetTimer();
    }

public:
    void onCreate() override
    {
        Init_Glitch_Pool();
        m_scroll_offset = 0;
        Init_Glitch_Pool();
        m_scroll_offset = 0;

        // 【新增 3】：开机时将真实的科幻音效瞬间吸入 PSRAM 高速内存
        File f_proc = LittleFS.open("/assets/procedure.wav", "r");
        if (f_proc)
        {
            wav_procedure_len = f_proc.size() - 44;
            wav_procedure_data = (uint8_t *)ps_malloc(wav_procedure_len);
            if (wav_procedure_data)
            {
                f_proc.seek(44);
                f_proc.read(wav_procedure_data, wav_procedure_len);
            }
            f_proc.close();
        }

        File f_fin = LittleFS.open("/assets/final.wav", "r");
        if (f_fin)
        {
            wav_final_len = f_fin.size() - 44;
            wav_final_data = (uint8_t *)ps_malloc(wav_final_len);
            if (wav_final_data)
            {
                f_fin.seek(44);
                f_fin.read(wav_final_data, wav_final_len);
            }
            f_fin.close();
        }

        extern int __internal_prescript_mode;
        extern int __internal_prescript_mode;
        if (__internal_prescript_mode != 2 && __internal_prescript_mode != 3)
        {
            m_state = S_WAIT_RELEASE;
        }
        else
        {
            m_state = S_DECODE;
        }
    }

    void onLoop() override
    {
        if (m_state == S_WAIT_RELEASE)
        {
            if (!HAL_Is_Key_Pressed())
                m_state = S_CHAOS;
        }
        else if (m_state == S_CHAOS)
        {
            drawChaosFrame(appManager.getLanguage());
            delay(ANIM_CHAOS_DELAY);
        }
        else if (m_state == S_DECODE)
        {
            executeDecodeSequence();
            m_state = S_DONE;
        }
    }

    void onDestroy() override
    {
        if (wav_procedure_data)
        {
            free(wav_procedure_data);
            wav_procedure_data = nullptr;
        }
        if (wav_final_data)
        {
            free(wav_final_data);
            wav_final_data = nullptr;
        }
        extern int __internal_prescript_mode;
        __internal_prescript_mode = 0;
    }

    void onKnob(int delta) override
    {
        if (m_state == S_DONE)
        {
            int max_vis = (appManager.getLanguage() == LANG_ZH) ? UI_CH_MAX_LINES : UI_EN_MAX_LINES;
            if (m_actual_lines > max_vis)
            {
                int max_offset = m_actual_lines - max_vis;
                int new_offset = m_scroll_offset + delta;

                if (new_offset < 0)
                    new_offset = 0;
                if (new_offset > max_offset)
                    new_offset = max_offset;

                if (new_offset != m_scroll_offset)
                {
                    m_scroll_offset = new_offset;
                    drawDoneFrame();
                    SYS_SOUND_NAV();
                }
            }
        }
    }

    void onKeyShort() override
    {
        if (m_state == S_CHAOS)
        {
            SYS_SOUND_NAV();
            m_state = S_DECODE;
        }
        else if (m_state == S_DONE)
        {
            SYS_SOUND_NAV();
            extern int __internal_prescript_mode;
            if (__internal_prescript_mode == 0)
                m_state = S_WAIT_RELEASE;
            else
                appManager.popApp();
        }
    }

    void onKeyLong() override
    {
        if (m_state == S_DONE || m_state == S_CHAOS)
            appManager.popApp();
    }
};

int __internal_prescript_mode = 0;
char __internal_custom_prescript[512] = {0};
void Prescript_Launch_PushNormal() { __internal_prescript_mode = 1; }
void Prescript_Launch_PushDirect() { __internal_prescript_mode = 2; }
void Prescript_Launch_Custom(const char *custom_text)
{
    __internal_prescript_mode = 3;
    snprintf(__internal_custom_prescript, sizeof(__internal_custom_prescript), "%s", custom_text);
}
void Prescript_Launch_Custom_Wait(const char *custom_text)
{
    __internal_prescript_mode = 4;
    snprintf(__internal_custom_prescript, sizeof(__internal_custom_prescript), "%s", custom_text);
}

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;