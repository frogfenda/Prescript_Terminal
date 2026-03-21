// 文件：src/apps/app_prescript_list.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_fs.h"
#include "sys_config.h"
#include "sys/sys_event.h"

// ========================================================
// 【新增】：独立的数据操作接口（暴露给未来的蓝牙、网络模块调用）
// ========================================================
void DBArchive_SaveToFile(SystemLang_t lang)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    const char *path = (lang == LANG_ZH) ? "/assets/prescripts_zh.txt" : "/assets/prescripts_en.txt";
    File f = LittleFS.open(path, "w");
    if (f)
    {
        for (size_t i = 0; i < p->size(); i++)
        {
            f.println((*p)[i]);
        }
        f.close();
    }
}

// ==========================================
// 邮局拆包回调：接收到蓝牙/NFC传来的新指令
// ==========================================
void _Cb_PreAdd(void *payload)
{
    Evt_PreAdd_t *p = (Evt_PreAdd_t *)payload;
    
    String new_text = String(p->text);
    new_text.trim(); 
    if (new_text.length() == 0) return; 

    extern void DBArchive_AddRecord(SystemLang_t lang, const String &text);
    DBArchive_AddRecord((SystemLang_t)p->lang, new_text);

    // 【真正的修复点】：用 LANG_ZH 来判断，彻底杜绝 0 和 1 的乌龙！
    Serial.printf("[指令库] 成功添加指令到 %s 库: %s\n", 
                 (p->lang == LANG_ZH) ? "中文" : "英文", 
                 new_text.c_str());
}

// 接口 1：从外部添加一条新指令到硬盘
void DBArchive_AddRecord(SystemLang_t lang, const String &text)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    p->push_back(text);
    DBArchive_SaveToFile(lang);
}

// 接口 2：从外部物理删除某一条指令
bool DBArchive_DeleteRecord(SystemLang_t lang, int index)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    if (index >= 0 && index < p->size())
    {
        p->erase(p->begin() + index);
        DBArchive_SaveToFile(lang);
        return true;
    }
    return false;
}
void _Cb_PreDel(void* payload) {
    Evt_PreDel_t* p = (Evt_PreDel_t*)payload;
    String target = String(p->text);
    SystemLang_t target_lang = (SystemLang_t)p->lang;
    
    // 声明你在上方写好的外部接口和存储数组
    extern bool DBArchive_DeleteRecord(SystemLang_t lang, int index);
    extern std::vector<String> sys_prescripts_zh;
    extern std::vector<String> sys_prescripts_en;

    // 根据传入的语言，选择对应的指令池
    std::vector<String> *target_pool = (target_lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    
    // 遍历寻找匹配的指令并删除
    for (int i = 0; i < target_pool->size(); i++) {
        if ((*target_pool)[i] == target) {
            DBArchive_DeleteRecord(target_lang, i);
            Serial.printf("[指令库] 成功抹除指令: %s\n", target.c_str());
            break; // 找到并删除后，立刻停止遍历
        }
    }
}
// ========================================================

class AppPrescriptList : public AppBase
{
private:
    std::vector<String> *pool;
    const char *file_path;

    int current_idx = 0;
    int m_scroll_offset = 0;
    bool m_is_deleting = false;

    static const int MAX_LINES = 30;
    char m_lines[MAX_LINES][128];
    int m_actual_lines = 0;

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

    void FormatCurrent()
    {
        if (pool->empty())
        {
            m_actual_lines = 0;
            return;
        }
        const char *text = (*pool)[current_idx].c_str();
        m_actual_lines = 0;

        if (appManager.getLanguage() == LANG_ZH)
        {
            int max_w = 42;
            char line_buf[128];
            int buf_idx = 0;
            int current_w = 0;

            for (int i = 0; text[i] != '\0';)
            {
                int clen = get_utf8_char_len(text[i]);
                int cw = (clen > 1) ? 2 : 1;

                if (current_w + cw > max_w || text[i] == '\n')
                {
                    line_buf[buf_idx] = '\0';
                    strncpy(m_lines[m_actual_lines++], line_buf, 128);
                    buf_idx = 0;
                    current_w = 0;
                    if (m_actual_lines >= MAX_LINES)
                        break;
                    if (text[i] == '\n')
                    {
                        i++;
                        continue;
                    }
                }

                for (int b = 0; b < clen; b++)
                {
                    if (buf_idx < 127)
                        line_buf[buf_idx++] = text[i];
                    i++;
                }
                current_w += cw;
            }
            if (buf_idx > 0 && m_actual_lines < MAX_LINES)
            {
                line_buf[buf_idx] = '\0';
                strncpy(m_lines[m_actual_lines++], line_buf, 128);
            }
        }
        else
        {
            int max_w = 46;
            char line_buf[128];
            int buf_idx = 0;
            int current_w = 0;
            int i = 0;

            while (text[i] != '\0' && m_actual_lines < MAX_LINES)
            {
                if (text[i] == '\n')
                {
                    line_buf[buf_idx] = '\0';
                    strncpy(m_lines[m_actual_lines++], line_buf, 128);
                    buf_idx = 0;
                    current_w = 0;
                    i++;
                    continue;
                }
                if (current_w == 0 && text[i] == ' ')
                {
                    i++;
                    continue;
                }

                int scan_i = i;
                int w_len = 0;
                while (text[scan_i] != '\0' && text[scan_i] != ' ' && text[scan_i] != '\n')
                {
                    w_len += (get_utf8_char_len(text[scan_i]) > 1) ? 2 : 1;
                    scan_i += get_utf8_char_len(text[scan_i]);
                }

                if (current_w > 0 && current_w + w_len > max_w)
                {
                    line_buf[buf_idx] = '\0';
                    strncpy(m_lines[m_actual_lines++], line_buf, 128);
                    buf_idx = 0;
                    current_w = 0;
                    continue;
                }

                while (i < scan_i)
                {
                    int clen = get_utf8_char_len(text[i]);
                    int cw = (clen > 1) ? 2 : 1;
                    if (current_w + cw > max_w)
                    {
                        line_buf[buf_idx] = '\0';
                        if (m_actual_lines < MAX_LINES)
                            strncpy(m_lines[m_actual_lines++], line_buf, 128);
                        buf_idx = 0;
                        current_w = 0;
                    }
                    for (int b = 0; b < clen; b++)
                    {
                        if (buf_idx < 127)
                            line_buf[buf_idx++] = text[i];
                        i++;
                    }
                    current_w += cw;
                }

                if (text[i] == ' ')
                {
                    if (current_w < max_w && buf_idx < 127)
                    {
                        line_buf[buf_idx++] = ' ';
                        current_w++;
                    }
                    i++;
                }
            }
            if (buf_idx > 0 && m_actual_lines < MAX_LINES)
            {
                line_buf[buf_idx] = '\0';
                strncpy(m_lines[m_actual_lines++], line_buf, 128);
            }
        }
    }

    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        SystemLang_t lang = appManager.getLanguage();

        if (pool->empty())
        {
            const char *empty_str = (lang == LANG_ZH) ? "数据库为空" : "DB ARCHIVE EMPTY";
            int ew = HAL_Get_Text_Width(empty_str);
            if (lang == LANG_ZH)
                HAL_Screen_ShowChineseLine((sw - ew) / 2, sh / 2 - 12, empty_str);
            else
                HAL_Screen_ShowTextLine((sw - ew) / 2, sh / 2 - 6, empty_str);
            HAL_Screen_Update();
            return;
        }

        int max_vis = (lang == LANG_ZH) ? 3 : 4;
        int row_h = (lang == LANG_ZH) ? 16 : 12;

        int draw_count = (m_actual_lines - m_scroll_offset > max_vis) ? max_vis : (m_actual_lines - m_scroll_offset);
        if (draw_count < 0)
            draw_count = 0;

        int block_h = draw_count * row_h;
        int start_y = (60 - block_h) / 2;
        if (start_y < 2)
            start_y = 2;

        int base_x = 4;
        if (lang == LANG_ZH)
        {
            int max_line_w = 0;
            for (int i = 0; i < draw_count; i++)
            {
                int w = HAL_Get_Text_Width(m_lines[m_scroll_offset + i]);
                if (w > max_line_w)
                    max_line_w = w;
            }
            base_x = (sw - max_line_w) / 2;
            if (base_x < 6)
                base_x = 6;
        }

        for (int i = 0; i < draw_count; i++)
        {
            int line_idx = m_scroll_offset + i;
            if (line_idx < m_actual_lines)
            {
                if (lang == LANG_ZH)
                {
                    HAL_Screen_ShowChineseLine(base_x, start_y, m_lines[line_idx]);
                }
                else
                {
                    HAL_Screen_ShowTextLine(base_x, start_y + 2, m_lines[line_idx]);
                }
                start_y += row_h;
            }
        }

        if (m_actual_lines > max_vis)
        {
            int max_offset = m_actual_lines - max_vis;
            int track_h = 40;
            int bar_h = track_h * max_vis / m_actual_lines;
            if (bar_h < 4)
                bar_h = 4;
            int bar_y = 10 + (track_h - bar_h) * m_scroll_offset / max_offset;
            HAL_Fill_Rect(sw - 4, bar_y, 2, bar_h, 1);
        }

        int bot_y = sh - 12;

        if (m_is_deleting)
        {
            HAL_Fill_Rect(12, sh / 2 - 16, sw - 24, 32, 0);
            HAL_Draw_Rect(12, sh / 2 - 16, sw - 24, 32, 1);

            if (lang == LANG_ZH)
            {
                const char *warn = "确认永久剥离此记录？";
                int ww = HAL_Get_Text_Width(warn);
                HAL_Screen_ShowChineseLine((sw - ww) / 2, sh / 2 - 6, warn);

                HAL_Screen_ShowChineseLine(4, bot_y - 4, "短按取消");
                const char *right_hint = "长按确认";
                int rw = HAL_Get_Text_Width(right_hint);
                HAL_Screen_ShowChineseLine(sw - rw - 4, bot_y - 4, right_hint);
            }
            else
            {
                const char *warn = "PERMANENTLY DELETE?";
                int ww = HAL_Get_Text_Width(warn);
                HAL_Screen_ShowTextLine((sw - ww) / 2, sh / 2 - 4, warn);

                HAL_Screen_ShowTextLine(4, bot_y, "SHORT:CANCEL");
                const char *right_hint = "HOLD:YES";
                int rw = HAL_Get_Text_Width(right_hint);
                HAL_Screen_ShowTextLine(sw - rw - 4, bot_y, right_hint);
            }
        }
        else
        {
            if (lang == LANG_ZH)
            {
                HAL_Screen_ShowChineseLine(4, bot_y - 4, "短按删除");
                const char *right_hint = "长按退出";
                int rw = HAL_Get_Text_Width(right_hint);
                HAL_Screen_ShowChineseLine(sw - rw - 4, bot_y - 4, right_hint);
            }
            else
            {
                HAL_Screen_ShowTextLine(4, bot_y, "SHORT:DEL");
                const char *right_hint = "HOLD:QUIT";
                int rw = HAL_Get_Text_Width(right_hint);
                HAL_Screen_ShowTextLine(sw - rw - 4, bot_y, right_hint);
            }

            char page_buf[32];
            snprintf(page_buf, sizeof(page_buf), "- %d / %d -", current_idx + 1, (int)pool->size());
            int pw = HAL_Get_Text_Width(page_buf);
            HAL_Screen_ShowTextLine((sw - pw) / 2, bot_y, page_buf);
        }

        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        if (appManager.getLanguage() == LANG_ZH)
        {
            pool = &sys_prescripts_zh;
            file_path = "/assets/prescripts_zh.txt";
        }
        else
        {
            pool = &sys_prescripts_en;
            file_path = "/assets/prescripts_en.txt";
        }
        current_idx = 0;
        m_scroll_offset = 0;
        m_is_deleting = false;
        FormatCurrent();
        drawUI();
    }

    void onResume() override { drawUI(); }
    void onLoop() override {}

    void onKnob(int delta) override
    {
        if (pool->empty())
            return;

        if (m_is_deleting)
        {
            m_is_deleting = false;
            SYS_SOUND_NAV();
            drawUI();
            return;
        }

        m_scroll_offset += delta;
        int max_vis = (appManager.getLanguage() == LANG_ZH) ? 3 : 4;
        int max_offset = (m_actual_lines > max_vis) ? (m_actual_lines - max_vis) : 0;

        if (m_scroll_offset < 0)
        {
            if (current_idx > 0)
            {
                current_idx--;
            }
            else
            {
                current_idx = pool->size() - 1;
            }
            FormatCurrent();
            max_offset = (m_actual_lines > max_vis) ? (m_actual_lines - max_vis) : 0;
            m_scroll_offset = max_offset;
        }
        else if (m_scroll_offset > max_offset)
        {
            if (current_idx < pool->size() - 1)
            {
                current_idx++;
            }
            else
            {
                current_idx = 0;
            }
            FormatCurrent();
            m_scroll_offset = 0;
        }

        SYS_SOUND_NAV();
        drawUI();
    }

    void onKeyShort() override
    {
        if (pool->empty())
        {
            appManager.popApp();
            return;
        }

        SYS_SOUND_NAV();
        m_is_deleting = !m_is_deleting;
        drawUI();
    }

    void onKeyLong() override
    {
        if (m_is_deleting)
        {
            if (!pool->empty())
            {
                HAL_Sprite_Clear();

                if (appManager.getLanguage() == LANG_ZH)
                {
                    const char *wipe_msg = "正在执行物理剥离...";
                    int wipe_w = HAL_Get_Text_Width(wipe_msg);
                    HAL_Screen_ShowChineseLine((HAL_Get_Screen_Width() - wipe_w) / 2, 30, wipe_msg);
                }
                else
                {
                    const char *wipe_msg = "PURGING RECORD...";
                    int wipe_w = HAL_Get_Text_Width(wipe_msg);
                    HAL_Screen_ShowTextLine((HAL_Get_Screen_Width() - wipe_w) / 2, 36, wipe_msg);
                }
                HAL_Screen_Update();

                SYS_SOUND_GLITCH();
                delay(300);

                // 【调用全新的外部安全接口进行物理删除】
                DBArchive_DeleteRecord(appManager.getLanguage(), current_idx);

                if (current_idx >= pool->size())
                    current_idx = pool->size() - 1;
                if (current_idx < 0)
                    current_idx = 0;

                m_is_deleting = false;
                FormatCurrent();
                m_scroll_offset = 0;
                drawUI();
            }
        }
        else
        {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
    void onDestroy() override {}

public:
    void onSystemInit() override
    {
        SysEvent_Subscribe(EVT_PRESCRIPT_ADD, _Cb_PreAdd);
        SysEvent_Subscribe(EVT_PRESCRIPT_DEL, _Cb_PreDel); // 【新增】：认领删除任务
        appManager.registerBackgroundApp(this);
    }
};

AppPrescriptList instancePrescriptList;
AppBase *appPrescriptList = &instancePrescriptList;