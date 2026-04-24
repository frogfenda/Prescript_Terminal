// 文件：src/apps/app_prescript.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_fs.h"
#include "sys_auto_push.h"
#include "sys_config.h"
#include <LittleFS.h>
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys/sys_res.h"
#include "sys_haptic.h"
#include "sys_specials.h"
bool g_prescript_needs_roll = true;

void Prescript_Prepare_PreRolled()
{
    g_prescript_needs_roll = false; // 被推送唤醒时调用，告诉自己不要重新摇号
}
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
        if (!g_wav_procedure)
            SYS_SOUND_GLITCH();
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
                HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + row * UI_CH_ROW_HEIGHT, row_buf,sysSpecials.getResult().color);
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
                HAL_Screen_ShowChineseLine_Color(margin_x, start_y + i * row_h, m_lines[r], sysSpecials.getResult().color);
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
        static String fs_rule_str;

        DrawResult res = sysSpecials.getResult();
        const char *rule = res.text.c_str();

        static char raw_prescript[1024];
        static char formatted_buf[2048];
        snprintf(raw_prescript, sizeof(raw_prescript), "_%s_", rule);
        raw_prescript[sizeof(raw_prescript) - 1] = '\0';

        // ==========================================
        // 【核心点燃】：在长达数秒的动画循环开始前，命令副核无限循环 procedure 音效！
        // ==========================================
        if (g_wav_procedure)
        {
            // 【修改】：不再操作 g_audio_loop，直接用 playWAV 的第三个参数 true 开启循环！
            sysAudio.playWAV(g_wav_procedure, g_wav_procedure_len, true);
        }

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
                                HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf, sysSpecials.getResult().color);
                                int cursor_x = UI_CH_MARGIN_X + HAL_Get_Text_Width(print_buf);
                                HAL_Fill_Rect(cursor_x, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT + 2, clen > 1 ? 12 : 6, 12, 1);
                            }
                            current_char++;
                            i += clen;
                        }
                        if (buf_idx > 0 && current_char <= frame)
                        {
                            print_buf[buf_idx] = '\0';
                            HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf, sysSpecials.getResult().color);
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
                                HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf, sysSpecials.getResult().color);
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
                            HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, print_buf, sysSpecials.getResult().color);
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
                            HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT, row_buf, sysSpecials.getResult().color);
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
            // === 英文解码部分 ===
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
                        int buf_idx = 0, current_w = 0;
                        if (r < draw_lines)
                        {
                            while (m_lines[r][current_w] != '\0' && current_w < UI_EN_MAX_COLS - 1)
                            {
                                if (frame >= lucky[r][current_w])
                                {
                                    row_buf[buf_idx++] = m_lines[r][current_w];
                                }
                                else
                                {
                                    all_locked = 0;
                                    row_buf[buf_idx++] = 33 + random(94);
                                }
                                current_w++;
                            }
                        }
                        while (current_w < UI_EN_MAX_COLS - 1)
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

        // ==========================================
        // 【终极打断】：强行解除硬件无限循环，并切歌！
        // ==========================================
        sysAudio.stopWAV(); // 【修改】：一键优雅停止
        if (g_wav_final)
        {
            // 参数3为 false，代表这声极具压迫感的解码音效只播放一次
            sysAudio.playWAV(g_wav_final, g_wav_final_len, false);
            SYS_HAPTIC_DECODE();
        }
        else
        {
            // 文件没读到的兜底保护
            SYS_SOUND_SUCCESS_4BEEPS();
        }

        SysAutoPush_ResetTimer();
    }

public:
    void onCreate() override
    {
        Init_Glitch_Pool();
        m_scroll_offset = 0;

        // 核心跳转：如果命运已定(弹窗进来的)，直接看结果；如果是手动进的，排队抽卡
        if (g_prescript_needs_roll)
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
            {
                // 1. 摇号
                sysSpecials.rollRandom();

                // 2. 【核心拦截】：如果抽中了特殊指令，立刻跳回弹窗界面！
                if (sysSpecials.getResult().is_special)
                {
                    extern void Prescript_Prepare_PreRolled();
                    Prescript_Prepare_PreRolled();        // 锁定命运
                    appManager.replaceApp(appPushNotify); // 强制替换成弹窗 App
                    return;
                }

                // 3. 如果是普通指令，才继续走自己的乱码流程
                m_state = S_CHAOS;
            }
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
        sysAudio.stopWAV();
        delay(5);
        g_prescript_needs_roll = true; // 退出时重置状态，保证下次手动进能正常抽卡
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
            if (g_prescript_needs_roll)
            {
                // 重新抽取时也要检测拦截
                sysSpecials.rollRandom();
                if (sysSpecials.getResult().is_special)
                {
                    extern void Prescript_Prepare_PreRolled();
                    Prescript_Prepare_PreRolled();
                    appManager.replaceApp(appPushNotify);
                    return;
                }
                m_state = S_WAIT_RELEASE;
            }
            else
            {
                appManager.popApp();
            }
        }
    }
    void onKeyLong() override
    {
        if (m_state == S_DONE || m_state == S_CHAOS)
            appManager.popApp();
    }
    // ==========================================
    // 【新增】：新侧边按键 (Btn2) 的专属逻辑
    // ==========================================
    void onBtn2Short() override
    {
        // 短按逻辑与旋钮按下完全一致：触发解码 / 抽取下一条
        onKeyShort();
    }

    void onBtn2Long() override
    {
        // 长按逻辑：清脆退出，返回上一级
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppPrescript instancePrescript;
AppBase *appPrescript = &instancePrescript;