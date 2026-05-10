/*
【模块职责】指令档案页。管理普通指令库的添加、删除、浏览、换行和滚动显示，并接收 PRE/PRE_DEL 事件。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_prescript_list.cpp
#include "app_base.h"
#include "app_manager.h"
#include "sys_fs.h"
#include "sys_config.h"
#include "sys/sys_event.h"
#include "sys/sys_command_result.h"
#include "../lang/terminal_lang.h"

// ========================================================
// 【新增】：独立的数据操作接口（暴露给未来的蓝牙、网络模块调用）
// ========================================================
// 【函数说明】把当前语言的指令池重写回 prescripts_zh/en.txt，每条指令一行，删除和新增后都通过它持久化。
bool DBArchive_SaveToFile(SystemLang_t lang)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    const char *path = TerminalLang::PrescriptPath(lang);
    File f = LittleFS.open(path, "w");
    if (!f)
        return false;

    for (size_t i = 0; i < p->size(); i++)
    {
        f.println((*p)[i]);
    }
    f.close();
    return true;
}

// ==========================================
// 邮局拆包回调：接收到蓝牙/NFC传来的新指令
// ==========================================
// 【函数说明】处理 PRE 命令：检查语言锁定、去重后加入对应指令池，保存文件并写业务 ACK。
void _Cb_PreAdd(void *payload)
{
    Evt_PreAdd_t *p = (Evt_PreAdd_t *)payload;

    if (!TerminalLang::Accepts((SystemLang_t)p->lang))
    {
        SysCmdResult_Error("LANG_LOCKED", TerminalLang::BUILD_CODE);
        return;
    }

    String new_text = String(p->text);
    new_text.trim();
    if (new_text.length() == 0)
    {
        SysCmdResult_Error("EMPTY_TEXT");
        return;
    }

    // 【函数说明】向指定语言指令池追加一条文本；先去重，再写文件，成功后内存池和 LittleFS 保持一致。
    extern bool DBArchive_AddRecord(SystemLang_t lang, const String &text);
    bool added = DBArchive_AddRecord((SystemLang_t)p->lang, new_text);

    Serial.printf("[指令库] %s添加指令到 %s 库: %s\n",
                  added ? "成功" : "未",
                  (p->lang == LANG_ZH) ? "中文" : "英文",
                  new_text.c_str());
}

// 接口 1：从外部添加一条新指令到硬盘
// 【函数说明】向指定语言指令池追加一条文本；先去重，再写文件，成功后内存池和 LittleFS 保持一致。
bool DBArchive_AddRecord(SystemLang_t lang, const String &text)
{
    if (!TerminalLang::Accepts(lang))
    {
        SysCmdResult_Error("LANG_LOCKED", TerminalLang::BUILD_CODE);
        return false;
    }
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    for (size_t i = 0; i < p->size(); i++)
    {
        if ((*p)[i] == text)
        {
            SysCmdResult_Warn("EXISTS");
            return false;
        }
    }

    p->push_back(text);
    if (!DBArchive_SaveToFile(lang))
    {
        p->pop_back();
        SysCmdResult_Error("SAVE_FAILED");
        return false;
    }

    SysCmdResult_Ok("ADDED");
    return true;
}

// 接口 2：从外部物理删除某一条指令
// 【函数说明】按索引从指定语言指令池删除一条记录，删除后重写文件。
bool DBArchive_DeleteRecord(SystemLang_t lang, int index)
{
    if (!TerminalLang::Accepts(lang))
    {
        SysCmdResult_Error("LANG_LOCKED", TerminalLang::BUILD_CODE);
        return false;
    }
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    if (index >= 0 && index < (int)p->size())
    {
        String removed = (*p)[index];
        p->erase(p->begin() + index);
        if (!DBArchive_SaveToFile(lang))
        {
            p->insert(p->begin() + index, removed);
            SysCmdResult_Error("SAVE_FAILED");
            return false;
        }
        SysCmdResult_Ok("DELETED");
        return true;
    }
    SysCmdResult_Error("NOT_FOUND");
    return false;
}

// 【函数说明】处理 PRE_DEL 命令：按文本找到对应语言池中的条目，删除并保存，找不到则返回 WARN。
void _Cb_PreDel(void* payload) {
    Evt_PreDel_t* p = (Evt_PreDel_t*)payload;
    String target = String(p->text);
    target.trim();
    if (target.length() == 0)
    {
        SysCmdResult_Error("EMPTY_TEXT");
        return;
    }
    SystemLang_t target_lang = (SystemLang_t)p->lang;
    if (!TerminalLang::Accepts(target_lang))
    {
        SysCmdResult_Error("LANG_LOCKED", TerminalLang::BUILD_CODE);
        return;
    }

    // 【函数说明】按索引从指定语言指令池删除一条记录，删除后重写文件。
    extern bool DBArchive_DeleteRecord(SystemLang_t lang, int index);
    extern std::vector<String> sys_prescripts_zh;
    extern std::vector<String> sys_prescripts_en;

    std::vector<String> *target_pool = (target_lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;

    for (int i = 0; i < (int)target_pool->size(); i++) {
        if ((*target_pool)[i] == target) {
            if (DBArchive_DeleteRecord(target_lang, i))
                Serial.printf("[指令库] 成功抹除指令: %s\n", target.c_str());
            return;
        }
    }

    SysCmdResult_Error("NOT_FOUND");
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

    // 【函数说明】返回当前 UTF-8 字符字节数，指令档案分行时避免切断中文字符。
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

    // 【函数说明】把当前选中的指令按屏幕宽度拆成多行，计算可滚动行数和 m_actual_lines。
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

    // 【函数说明】绘制指令档案页：顶部显示编号/总数，中间显示当前指令多行文本，右侧显示滚动条，删除模式显示危险确认。
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
    // 【函数说明】进入档案页时读取当前语言指令池，选择第一条并完成第一次分行。
    void onCreate() override
    {
        if (appManager.getLanguage() == LANG_ZH)
        {
            pool = &sys_prescripts_zh;
            file_path = TerminalLang::PrescriptPath(LANG_ZH);
        }
        else
        {
            pool = &sys_prescripts_en;
            file_path = TerminalLang::PrescriptPath(LANG_EN);
        }
        current_idx = 0;
        m_scroll_offset = 0;
        m_is_deleting = false;
        FormatCurrent();
        drawUI();
    }

    // 【函数说明】返回档案页时重绘当前记录，保留选中项和滚动偏移。
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

    // 【函数说明】普通模式短按进入/确认删除流程；删除确认后删除当前指令并重排索引。
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

    // 【函数说明】普通模式长按退出档案页；删除模式长按取消删除确认。
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
    // 【函数说明】离开档案页不释放指令池，池由 sys_fs 和全局 vector 持有。
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
