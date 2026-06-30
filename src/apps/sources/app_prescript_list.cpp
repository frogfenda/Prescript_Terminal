// 文件：src/apps/app_prescript_list.cpp
/*
【模块职责】指令档案 App。

本文件负责：
1. 显示当前语言的指令库文本；
2. 用旋钮在“当前指令内部滚动”和“上一条/下一条指令”之间连续切换；
3. 提供二次确认删除流程；
4. 接收 BLE/NFC/Web 路由发布的 PRE / PRE_DEL 事件并写入 LittleFS。

大屏适配说明：
- 旧版本按 284×76 写死 3~4 行显示、固定 40px 滚动条；
- 当前版本按 HAL_Get_Screen_Width/Height 和当前字体行高计算可见行数、正文宽度和滚动条；
- 删除交互改为“短按进入确认 → 短按继续删除 → 长按取消删除”，与用户当前交互约定一致。
*/
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_fs.h"
#include "sys/sys_config.h"
#include "sys/sys_event.h"
#include "lang/terminal_lang.h"
#include "lang/ui_strings.h"
#include "hal/hal.h"
#include "ui/ui_theme.h"
#include "ui/ui_frame.h"

// ========================================================
// 指令库文件写入接口：BLE/NFC/Web 与 App 页面共用
// ========================================================
void DBArchive_SaveToFile(SystemLang_t lang)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    const char *path = TerminalLang::PrescriptPath(lang);

    File f = LittleFS.open(path, "w");
    if (!f)
    {
        Serial.printf("[指令档案] 保存失败，无法打开文件：%s\n", path);
        return;
    }

    for (size_t i = 0; i < p->size(); i++)
    {
        f.println((*p)[i]);
    }
    f.close();

    Serial.printf("[指令档案] 已写入 %s，共 %u 条。\n", path, (unsigned)p->size());
}

// 【事件回调】收到 PRE 添加事件后，把新指令追加到当前语言对应的文本库。
void _Cb_PreAdd(void *payload)
{
    Evt_PreAdd_t *p = (Evt_PreAdd_t *)payload;
    if (!p || !p->text)
        return;

    String new_text = String(p->text);
    new_text.trim();
    if (new_text.length() == 0)
        return;

    extern void DBArchive_AddRecord(SystemLang_t lang, const String &text);
    DBArchive_AddRecord((SystemLang_t)p->lang, new_text);

    Serial.printf("[指令档案] 已添加到%s库：%s\n",
                  (p->lang == LANG_ZH) ? "中文" : "英文",
                  new_text.c_str());
}

// 【外部接口】添加一条指令并立即写回 LittleFS。
void DBArchive_AddRecord(SystemLang_t lang, const String &text)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    p->push_back(text);
    DBArchive_SaveToFile(lang);
}

// 【外部接口】按索引删除指令；删除成功后立即写回 LittleFS。
bool DBArchive_DeleteRecord(SystemLang_t lang, int index)
{
    std::vector<String> *p = (lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;
    if (index >= 0 && index < (int)p->size())
    {
        String removed = (*p)[index];
        p->erase(p->begin() + index);
        DBArchive_SaveToFile(lang);
        Serial.printf("[指令档案] 已删除记录：%s\n", removed.c_str());
        return true;
    }

    Serial.printf("[指令档案] 删除失败，索引越界：%d\n", index);
    return false;
}

// 【事件回调】收到 PRE_DEL 事件后，按文本内容查找并删除第一条完全匹配的指令。
void _Cb_PreDel(void* payload)
{
    Evt_PreDel_t* p = (Evt_PreDel_t*)payload;
    if (!p || !p->text)
        return;

    String target = String(p->text);
    SystemLang_t target_lang = (SystemLang_t)p->lang;

    std::vector<String> *target_pool = (target_lang == LANG_ZH) ? &sys_prescripts_zh : &sys_prescripts_en;

    for (int i = 0; i < (int)target_pool->size(); i++)
    {
        if ((*target_pool)[i] == target)
        {
            DBArchive_DeleteRecord(target_lang, i);
            Serial.printf("[指令档案] 已按文本删除：%s\n", target.c_str());
            return;
        }
    }

    Serial.printf("[指令档案] 未找到待删除文本：%s\n", target.c_str());
}

class AppPrescriptList : public AppBase
{
private:
    std::vector<String> *pool = nullptr;
    const char *file_path = nullptr;

    int current_idx = 0;
    int m_scroll_offset = 0;
    bool m_is_deleting = false;

    // 大屏下每行可容纳更多字符，因此单行缓存从 128 增加到 192。
    // MAX_LINES 用于限制极长指令的换行缓存，避免单条指令占用过多 RAM。
    static const int MAX_LINES = 40;
    static const int LINE_BUF_LEN = 192;
    char m_lines[MAX_LINES][LINE_BUF_LEN];
    int m_actual_lines = 0;

    int get_utf8_char_len(char c)
    {
        unsigned char uc = (unsigned char)c;
        if ((uc & 0x80) == 0) return 1;
        if ((uc & 0xE0) == 0xC0) return 2;
        if ((uc & 0xF0) == 0xE0) return 3;
        if ((uc & 0xF8) == 0xF0) return 4;
        return 1;
    }

    int bodyRowHeight() const
    {
        return HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 2;
    }

    int bottomHintY() const
    {
        return HAL_Get_Screen_Height() - HAL_Get_Font_Line_Height(HAL_FONT_SMALL) - 5;
    }

    int contentTopY() const
    {
        return HAL_Get_Font_Line_Height(HAL_FONT_SMALL) + 13;
    }

    int visibleLineCount() const
    {
        int available_h = bottomHintY() - contentTopY() - 8;
        int count = available_h / bodyRowHeight();
        return max(1, count);
    }

    int contentMaxWidth() const
    {
        int sw = HAL_Get_Screen_Width();
        int margin = max(22, sw / 16);
        int scroll_reserved = 14;
        return sw - margin * 2 - scroll_reserved;
    }

    void pushFormattedLine(const String& line)
    {
        if (m_actual_lines >= MAX_LINES)
            return;

        String safe = line;
        safe.trim();
        safe.toCharArray(m_lines[m_actual_lines], LINE_BUF_LEN);
        m_actual_lines++;
    }

    /**
     * 按当前字体真实像素宽度给当前指令换行。
     *
     * 旧版本按“中文 2、英文 1”的字符宽度估算，适配旧屏还可以，换成自定义字体后会不准。
     * 当前实现每次追加 UTF-8 字符前用 HAL_Get_Text_Width() 测量候选行宽，
     * 因此字体、字号或屏幕宽度变化后，换行会自动跟随。
     */
    void FormatCurrent()
    {
        m_actual_lines = 0;

        if (!pool || pool->empty())
            return;

        const String text = (*pool)[current_idx];
        const int max_px = contentMaxWidth();
        String line = "";

        for (int i = 0; i < (int)text.length() && m_actual_lines < MAX_LINES;)
        {
            if (text[i] == '\n')
            {
                pushFormattedLine(line);
                line = "";
                i++;
                continue;
            }

            int clen = get_utf8_char_len(text[i]);
            String token = text.substring(i, i + clen);
            String candidate = line + token;

            if (line.length() > 0 && HAL_Get_Text_Width(candidate.c_str()) > max_px)
            {
                pushFormattedLine(line);
                line = token;
            }
            else
            {
                line = candidate;
            }

            i += clen;
        }

        if (line.length() > 0 && m_actual_lines < MAX_LINES)
        {
            pushFormattedLine(line);
        }
    }

    void drawSmallText(int x, int y, const char* text, uint16_t color = TFT_DARKGREY)
    {
        HAL_Screen_ShowLine_Font(x, y, text, HAL_FONT_SMALL, color);
    }

    void drawCenteredSmall(int y, const char* text, uint16_t color = TFT_DARKGREY)
    {
        int sw = HAL_Get_Screen_Width();
        int x = (sw - HAL_Get_Text_Width_Font(text, HAL_FONT_SMALL)) / 2;
        drawSmallText(x, y, text, color);
    }

    void drawBottomHints(const char* left, const char* right, uint16_t color = TFT_DARKGREY)
    {
        int sw = HAL_Get_Screen_Width();
        int y = bottomHintY();
        drawSmallText(8, y, left, color);
        int rw = HAL_Get_Text_Width_Font(right, HAL_FONT_SMALL);
        drawSmallText(sw - rw - 8, y, right, color);
    }

    void drawDeleteConfirm()
    {
        SystemLang_t lang = appManager.getLanguage();
        UIFrame::DrawDangerConfirm(UIStrings::ArchiveDeleteTitle(lang), "", UIStrings::DeleteUserHint(lang));
    }

    void drawArchiveEmpty()
    {
        SystemLang_t lang = appManager.getLanguage();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        const char *empty_str = UIStrings::ArchiveEmpty(lang);
        int ew = HAL_Get_Text_Width(empty_str);
        HAL_Screen_ShowLine_Font((sw - ew) / 2,
                                 (sh - HAL_Get_Font_Line_Height(HAL_FONT_BODY)) / 2,
                                 empty_str,
                                 HAL_FONT_BODY,
                                 TFT_CYAN);
    }

    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        SystemLang_t lang = appManager.getLanguage();

        if (!pool || pool->empty())
        {
            drawArchiveEmpty();
            drawBottomHints(UIStrings::ClickBackHint(lang), UIStrings::HoldBackHint(lang));
            HAL_Screen_Update();
            return;
        }

        // 顶部显示当前页码。它使用小字体，不抢正文空间。
        char page_buf[40];
        snprintf(page_buf, sizeof(page_buf), "%d / %d", current_idx + 1, (int)pool->size());
        drawCenteredSmall(5, page_buf, TFT_DARKGREY);

        int max_vis = visibleLineCount();
        int draw_count = min(max_vis, m_actual_lines - m_scroll_offset);
        if (draw_count < 0)
            draw_count = 0;

        int row_h = bodyRowHeight();
        int content_top = contentTopY();
        int content_h = max_vis * row_h;
        int start_y = content_top + max(0, (bottomHintY() - content_top - content_h - 8) / 2);

        for (int i = 0; i < draw_count; i++)
        {
            int line_idx = m_scroll_offset + i;
            if (line_idx < m_actual_lines)
            {
                int line_w = HAL_Get_Text_Width(m_lines[line_idx]);
                int x = (sw - line_w) / 2;
                if (x < 16) x = 16;

                HAL_Screen_ShowLine_Font(x, start_y + i * row_h, m_lines[line_idx], HAL_FONT_BODY, TFT_CYAN);
            }
        }

        // 右侧滚动条根据正文区域高度计算，显示当前指令内部滚动位置。
        if (m_actual_lines > max_vis)
        {
            int max_offset = m_actual_lines - max_vis;
            int track_y = contentTopY();
            int track_h = bottomHintY() - track_y - 8;
            int bar_h = max(8, track_h * max_vis / m_actual_lines);
            int bar_y = track_y + (track_h - bar_h) * m_scroll_offset / max_offset;
            HAL_Fill_Rect(sw - 8, track_y, 1, track_h, TFT_DARKGREY);
            HAL_Fill_Rect(sw - 10, bar_y, 4, bar_h, TFT_CYAN);
        }

        if (m_is_deleting)
        {
            drawDeleteConfirm();
        }
        else
        {
            drawBottomHints(UIStrings::ClickDeleteHint(lang),
                            UIStrings::HoldExitHint(lang));
        }

        HAL_Screen_Update();
    }

    void deleteCurrentRecord()
    {
        if (!pool || pool->empty())
            return;

        HAL_Sprite_Clear();
        const char* wipe_msg = UIStrings::PurgingRecord(appManager.getLanguage());
        int x = (HAL_Get_Screen_Width() - HAL_Get_Text_Width(wipe_msg)) / 2;
        int y = (HAL_Get_Screen_Height() - HAL_Get_Font_Line_Height(HAL_FONT_BODY)) / 2;
        HAL_Screen_ShowLine_Font(x, y, wipe_msg, HAL_FONT_BODY, TFT_RED);
        HAL_Screen_Update();

        SYS_SOUND_GLITCH();
        delay(220);

        DBArchive_DeleteRecord(appManager.getLanguage(), current_idx);

        if (!pool || pool->empty())
        {
            current_idx = 0;
            m_scroll_offset = 0;
            m_is_deleting = false;
            m_actual_lines = 0;
            drawUI();
            return;
        }

        if (current_idx >= (int)pool->size())
            current_idx = (int)pool->size() - 1;
        if (current_idx < 0)
            current_idx = 0;

        m_is_deleting = false;
        m_scroll_offset = 0;
        FormatCurrent();
        drawUI();
    }

public:
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

    void onResume() override { drawUI(); }
    void onLoop() override {}

    void onKnob(int delta) override
    {
        if (!pool || pool->empty())
            return;

        // 删除确认状态下不响应旋钮，避免用户误转导致删除对象变化。
        if (m_is_deleting)
            return;

        m_scroll_offset += delta;
        int max_vis = visibleLineCount();
        int max_offset = (m_actual_lines > max_vis) ? (m_actual_lines - max_vis) : 0;

        if (m_scroll_offset < 0)
        {
            current_idx = (current_idx > 0) ? current_idx - 1 : (int)pool->size() - 1;
            FormatCurrent();
            max_offset = (m_actual_lines > max_vis) ? (m_actual_lines - max_vis) : 0;
            m_scroll_offset = max_offset;
        }
        else if (m_scroll_offset > max_offset)
        {
            current_idx = (current_idx < (int)pool->size() - 1) ? current_idx + 1 : 0;
            FormatCurrent();
            m_scroll_offset = 0;
        }

        SYS_SOUND_NAV();
        drawUI();
    }

    void onKeyShort() override
    {
        if (!pool || pool->empty())
        {
            appManager.popApp();
            return;
        }

        if (m_is_deleting)
        {
            // 新交互：短按进入确认后，再短按才真正删除。
            deleteCurrentRecord();
            return;
        }

        SYS_SOUND_NAV();
        m_is_deleting = true;
        drawUI();
    }

    void onKeyLong() override
    {
        if (m_is_deleting)
        {
            // 新交互：确认界面长按取消删除，不退出档案页。
            m_is_deleting = false;
            SYS_SOUND_NAV();
            drawUI();
            return;
        }

        SYS_SOUND_NAV();
        appManager.popApp();
    }

    void onDestroy() override {}

    void onSystemInit() override
    {
        SysEvent_Subscribe(EVT_PRESCRIPT_ADD, _Cb_PreAdd);
        SysEvent_Subscribe(EVT_PRESCRIPT_DEL, _Cb_PreDel);
        appManager.registerBackgroundApp(this);
    }
};

AppPrescriptList instancePrescriptList;
AppBase *appPrescriptList = &instancePrescriptList;
