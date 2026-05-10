/*
【模块职责】指令解码渲染实现。负责中英文换行、乱码字符池、四种解码动画、完成态滚动条，AppPrescript 只保留业务状态机。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "ui_prescript_decoder.h"
#include "ui_text.h"
#include "../hal/hal.h"

namespace UIPrescript {

namespace {

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

static const int ANIM1_FRAMES = 22;
static const int ANIM1_DELAY = 40;
static const int ANIM2_SPEED_DELAY = 30;
static const int ANIM3_GLITCH_COUNT = 4;
static const int ANIM3_GLITCH_DELAY = 20;
static const int ANIM4_BASE_FRAMES = 6;
static const int ANIM4_DELAY = 20;

static const int CACHE_SIZE = 300;
static char g_glitchPool[CACHE_SIZE][4];

const char* GLOBAL_CHINESE_DICT = "数据错误系统异常致命警告内存溢出未知指令核心损坏参数丢失连接中断身份拒绝权限不足矩阵终端序列重载覆写剥离重构解析编译代码节点网关漏洞端口渗透代理溯源封锁拦截劫持";

int utf8Len(const char* s)
{
    return UIText::Utf8CharLen((unsigned char)*s);
}

void procedure(ProcedureTick cb)
{
    if (cb) cb();
}

int splitToLines(const char* formatted, char lines[][256])
{
    int lineIdx = 0;
    int bufIdx = 0;
    for (int i = 0; formatted[i] != '\0'; ++i)
    {
        if (formatted[i] == '\n')
        {
            lines[lineIdx][bufIdx] = '\0';
            ++lineIdx;
            bufIdx = 0;
            if (lineIdx >= TextLayout::MaxLines) break;
        }
        else
        {
            if (bufIdx < 255) lines[lineIdx][bufIdx++] = formatted[i];
        }
    }

    if (bufIdx > 0 && lineIdx < TextLayout::MaxLines)
    {
        lines[lineIdx][bufIdx] = '\0';
        ++lineIdx;
    }
    return lineIdx;
}

void formatChineseToGrid(const char* raw, char* out)
{
    int currentW = 0;
    int outIdx = 0;
    for (int i = 0; raw[i] != '\0';)
    {
        if (raw[i] == '\n')
        {
            out[outIdx++] = raw[i++];
            currentW = 0;
            continue;
        }

        int clen = utf8Len(&raw[i]);
        int cw = (clen > 1) ? 2 : 1;
        if (currentW + cw > UI_CH_MAX_COLS)
        {
            out[outIdx++] = '\n';
            currentW = 0;
        }
        for (int b = 0; b < clen; ++b) out[outIdx++] = raw[i++];
        currentW += cw;
    }
    out[outIdx] = '\0';
}

void formatEnglishToGrid(const char* raw, char* out)
{
    int currentW = 0;
    int outIdx = 0;
    int i = 0;

    while (raw[i] != '\0')
    {
        if (raw[i] == '\n')
        {
            out[outIdx++] = '\n';
            currentW = 0;
            ++i;
            continue;
        }

        if (currentW == 0 && raw[i] == ' ')
        {
            ++i;
            continue;
        }

        int scan = i;
        int wordW = 0;
        while (raw[scan] != '\0' && raw[scan] != ' ' && raw[scan] != '\n')
        {
            int clen = utf8Len(&raw[scan]);
            wordW += (clen > 1) ? 2 : 1;
            scan += clen;
        }

        if (currentW > 0 && currentW + wordW > UI_EN_MAX_COLS)
        {
            out[outIdx++] = '\n';
            currentW = 0;
            continue;
        }

        while (i < scan)
        {
            int clen = utf8Len(&raw[i]);
            int cw = (clen > 1) ? 2 : 1;
            if (currentW + cw > UI_EN_MAX_COLS)
            {
                out[outIdx++] = '\n';
                currentW = 0;
            }
            for (int b = 0; b < clen; ++b) out[outIdx++] = raw[i++];
            currentW += cw;
        }

        if (raw[i] == ' ')
        {
            if (currentW < UI_EN_MAX_COLS)
            {
                out[outIdx++] = ' ';
                ++currentW;
            }
            ++i;
        }
    }
    out[outIdx] = '\0';
}

int countCharsZh(const TextLayout& layout, int drawLines)
{
    int total = 0;
    for (int r = 0; r < drawLines; ++r)
    {
        int i = 0;
        while (layout.lines[r][i] != '\0')
        {
            ++total;
            i += utf8Len(&layout.lines[r][i]);
        }
    }
    return total;
}

int countCharsEn(const TextLayout& layout, int drawLines)
{
    int total = 0;
    for (int r = 0; r < drawLines; ++r)
    {
        for (int i = 0; layout.lines[r][i] != '\0'; ++i) ++total;
    }
    return total;
}

bool isTargetSpaceZh(const TextLayout& layout, int drawLines, int target)
{
    int c = 0;
    for (int r = 0; r < drawLines; ++r)
    {
        int i = 0;
        while (layout.lines[r][i] != '\0')
        {
            if (c == target) return layout.lines[r][i] == ' ';
            ++c;
            i += utf8Len(&layout.lines[r][i]);
        }
    }
    return false;
}

bool isTargetSpaceEn(const TextLayout& layout, int drawLines, int target)
{
    int c = 0;
    for (int r = 0; r < drawLines; ++r)
    {
        for (int i = 0; layout.lines[r][i] != '\0'; ++i)
        {
            if (c == target) return layout.lines[r][i] == ' ';
            ++c;
        }
    }
    return false;
}

void drawLineZh(int row, const char* text, uint16_t color)
{
    HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + row * UI_CH_ROW_HEIGHT, text, color);
}

void drawLineEn(int row, const char* text)
{
    HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + row * UI_EN_ROW_HEIGHT, text);
}

void appendGlitchForChar(char* out, int& idx, int charLen, bool allowChinese)
{
    if (allowChinese && charLen > 1)
    {
        if (random(100) < 40)
        {
            int p = random(CACHE_SIZE);
            out[idx++] = g_glitchPool[p][0];
            out[idx++] = g_glitchPool[p][1];
            out[idx++] = g_glitchPool[p][2];
        }
        else
        {
            out[idx++] = 33 + random(94);
            out[idx++] = 33 + random(94);
        }
    }
    else
    {
        out[idx++] = 33 + random(94);
    }
}

void playCursorZh(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = countCharsZh(layout, drawLines);
    for (int frame = 0; frame <= totalChars; ++frame)
    {
        HAL_Sprite_Clear();
        int currentChar = 0;
        for (int r = 0; r < drawLines; ++r)
        {
            char printBuf[256] = {0};
            int bufIdx = 0;
            int i = 0;
            while (layout.lines[r][i] != '\0')
            {
                int clen = utf8Len(&layout.lines[r][i]);
                if (currentChar < frame)
                {
                    for (int b = 0; b < clen; ++b) printBuf[bufIdx++] = layout.lines[r][i + b];
                }
                else if (currentChar == frame)
                {
                    printBuf[bufIdx] = '\0';
                    drawLineZh(r, printBuf, layout.color);
                    int cursorX = UI_CH_MARGIN_X + HAL_Get_Text_Width(printBuf);
                    HAL_Fill_Rect(cursorX, UI_CH_START_Y + r * UI_CH_ROW_HEIGHT + 2, clen > 1 ? 12 : 6, 12, 1);
                }
                ++currentChar;
                i += clen;
            }
            if (bufIdx > 0 && currentChar <= frame)
            {
                printBuf[bufIdx] = '\0';
                drawLineZh(r, printBuf, layout.color);
            }
        }
        HAL_Screen_Update();
        if (frame < totalChars)
        {
            procedure(cb);
            delay(ANIM2_SPEED_DELAY);
        }
    }
}

void playGlitchOneByOneZh(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = countCharsZh(layout, drawLines);
    for (int target = 0; target <= totalChars; ++target)
    {
        bool isSpace = (target < totalChars) ? isTargetSpaceZh(layout, drawLines, target) : false;
        int frames = (isSpace || target == totalChars) ? 1 : ANIM3_GLITCH_COUNT;
        for (int f = 0; f < frames; ++f)
        {
            HAL_Sprite_Clear();
            int currentChar = 0;
            for (int r = 0; r < drawLines; ++r)
            {
                char printBuf[256] = {0};
                int bufIdx = 0;
                int i = 0;
                while (layout.lines[r][i] != '\0')
                {
                    int clen = utf8Len(&layout.lines[r][i]);
                    if (currentChar < target)
                    {
                        for (int b = 0; b < clen; ++b) printBuf[bufIdx++] = layout.lines[r][i + b];
                    }
                    else if (currentChar == target && target < totalChars)
                    {
                        if (isSpace) printBuf[bufIdx++] = ' ';
                        else appendGlitchForChar(printBuf, bufIdx, clen, true);
                    }
                    ++currentChar;
                    i += clen;
                }
                if (bufIdx > 0)
                {
                    printBuf[bufIdx] = '\0';
                    drawLineZh(r, printBuf, layout.color);
                }
            }
            HAL_Screen_Update();
            if (target < totalChars && !isSpace)
            {
                procedure(cb);
                delay(ANIM3_GLITCH_DELAY);
            }
        }
    }
}

void playGlobalWaveZh(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = countCharsZh(layout, drawLines);
    int totalFrames = totalChars + ANIM4_BASE_FRAMES;
    for (int frame = 0; frame <= totalFrames; ++frame)
    {
        HAL_Sprite_Clear();
        int currentChar = 0;
        for (int r = 0; r < drawLines; ++r)
        {
            char printBuf[256] = {0};
            int bufIdx = 0;
            int i = 0;
            while (layout.lines[r][i] != '\0')
            {
                int clen = utf8Len(&layout.lines[r][i]);
                int resolveFrame = currentChar + ANIM4_BASE_FRAMES;
                if (frame >= resolveFrame)
                {
                    for (int b = 0; b < clen; ++b) printBuf[bufIdx++] = layout.lines[r][i + b];
                }
                else
                {
                    if (layout.lines[r][i] == ' ') printBuf[bufIdx++] = ' ';
                    else appendGlitchForChar(printBuf, bufIdx, clen, true);
                }
                ++currentChar;
                i += clen;
            }
            if (bufIdx > 0)
            {
                printBuf[bufIdx] = '\0';
                drawLineZh(r, printBuf, layout.color);
            }
        }
        HAL_Screen_Update();
        if (frame < totalFrames)
        {
            procedure(cb);
            delay(ANIM4_DELAY);
        }
    }
}

void playMatrixLockZh(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    uint8_t lucky[UI_CH_MAX_LINES][60];
    for (int r = 0; r < UI_CH_MAX_LINES; ++r)
        for (int c = 0; c < UI_CH_MAX_COLS; ++c)
            lucky[r][c] = random(11) + 2;

    for (int frame = 0; frame < ANIM1_FRAMES; ++frame)
    {
        uint8_t allLocked = 1;
        HAL_Sprite_Clear();
        for (int r = 0; r < UI_CH_MAX_LINES; ++r)
        {
            char rowBuf[512];
            int bufIdx = 0;
            int currentW = 0;
            int byteIdx = 0;
            if (r < drawLines)
            {
                while (layout.lines[r][byteIdx] != '\0' && currentW < UI_CH_MAX_COLS - 1)
                {
                    int clen = utf8Len(&layout.lines[r][byteIdx]);
                    int cw = (clen > 1) ? 2 : 1;
                    if (frame >= lucky[r][currentW])
                    {
                        for (int b = 0; b < clen; ++b) rowBuf[bufIdx++] = layout.lines[r][byteIdx + b];
                    }
                    else
                    {
                        allLocked = 0;
                        appendGlitchForChar(rowBuf, bufIdx, clen, true);
                    }
                    currentW += cw;
                    byteIdx += clen;
                }
            }
            while (currentW < UI_CH_MAX_COLS - 1)
            {
                if (frame < lucky[r][currentW])
                {
                    allLocked = 0;
                    if (random(100) < 40 && currentW <= UI_CH_MAX_COLS - 2)
                    {
                        int p = random(CACHE_SIZE);
                        rowBuf[bufIdx++] = g_glitchPool[p][0];
                        rowBuf[bufIdx++] = g_glitchPool[p][1];
                        rowBuf[bufIdx++] = g_glitchPool[p][2];
                        currentW += 2;
                    }
                    else
                    {
                        rowBuf[bufIdx++] = 33 + random(94);
                        currentW += 1;
                    }
                }
                else
                {
                    rowBuf[bufIdx++] = ' ';
                    currentW += 1;
                }
            }
            rowBuf[bufIdx] = '\0';
            if (bufIdx > 0) drawLineZh(r, rowBuf, layout.color);
        }
        HAL_Screen_Update();
        if (!allLocked) procedure(cb);
        if (allLocked) break;
        delay(ANIM1_DELAY);
        yield();
    }
}

void playCursorEn(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = countCharsEn(layout, drawLines);
    for (int frame = 0; frame <= totalChars; ++frame)
    {
        HAL_Sprite_Clear();
        int currentChar = 0;
        for (int r = 0; r < drawLines; ++r)
        {
            char printBuf[256] = {0};
            int bufIdx = 0;
            int i = 0;
            while (layout.lines[r][i] != '\0')
            {
                if (currentChar < frame) printBuf[bufIdx++] = layout.lines[r][i];
                else if (currentChar == frame)
                {
                    printBuf[bufIdx] = '\0';
                    drawLineEn(r, printBuf);
                    int cursorX = UI_EN_MARGIN_X + bufIdx * 6;
                    HAL_Fill_Rect(cursorX, UI_EN_START_Y + r * UI_EN_ROW_HEIGHT, 6, 12, 1);
                }
                ++currentChar;
                ++i;
            }
            if (bufIdx > 0 && currentChar <= frame)
            {
                printBuf[bufIdx] = '\0';
                drawLineEn(r, printBuf);
            }
        }
        HAL_Screen_Update();
        if (frame < totalChars)
        {
            procedure(cb);
            delay(ANIM2_SPEED_DELAY);
        }
    }
}

void playGlitchOneByOneEn(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = countCharsEn(layout, drawLines);
    for (int target = 0; target <= totalChars; ++target)
    {
        bool isSpace = (target < totalChars) ? isTargetSpaceEn(layout, drawLines, target) : false;
        int frames = (isSpace || target == totalChars) ? 1 : ANIM3_GLITCH_COUNT;
        for (int f = 0; f < frames; ++f)
        {
            HAL_Sprite_Clear();
            int currentChar = 0;
            for (int r = 0; r < drawLines; ++r)
            {
                char printBuf[256] = {0};
                int bufIdx = 0;
                int i = 0;
                while (layout.lines[r][i] != '\0')
                {
                    if (currentChar < target) printBuf[bufIdx++] = layout.lines[r][i];
                    else if (currentChar == target && target < totalChars)
                    {
                        if (isSpace) printBuf[bufIdx++] = ' ';
                        else printBuf[bufIdx++] = 33 + random(94);
                    }
                    ++currentChar;
                    ++i;
                }
                if (bufIdx > 0)
                {
                    printBuf[bufIdx] = '\0';
                    drawLineEn(r, printBuf);
                }
            }
            HAL_Screen_Update();
            if (target < totalChars && !isSpace)
            {
                procedure(cb);
                delay(ANIM3_GLITCH_DELAY);
            }
        }
    }
}

void playGlobalWaveEn(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = countCharsEn(layout, drawLines);
    int totalFrames = totalChars + ANIM4_BASE_FRAMES;
    for (int frame = 0; frame <= totalFrames; ++frame)
    {
        HAL_Sprite_Clear();
        int currentChar = 0;
        for (int r = 0; r < drawLines; ++r)
        {
            char printBuf[256] = {0};
            int bufIdx = 0;
            int i = 0;
            while (layout.lines[r][i] != '\0')
            {
                int resolveFrame = currentChar + ANIM4_BASE_FRAMES;
                if (frame >= resolveFrame) printBuf[bufIdx++] = layout.lines[r][i];
                else
                {
                    if (layout.lines[r][i] == ' ') printBuf[bufIdx++] = ' ';
                    else printBuf[bufIdx++] = 33 + random(94);
                }
                ++currentChar;
                ++i;
            }
            if (bufIdx > 0)
            {
                printBuf[bufIdx] = '\0';
                drawLineEn(r, printBuf);
            }
        }
        HAL_Screen_Update();
        if (frame < totalFrames)
        {
            procedure(cb);
            delay(ANIM4_DELAY);
        }
    }
}

void playMatrixLockEn(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    uint8_t lucky[UI_EN_MAX_LINES][60];
    for (int r = 0; r < UI_EN_MAX_LINES; ++r)
        for (int c = 0; c < UI_EN_MAX_COLS; ++c)
            lucky[r][c] = random(11) + 2;

    for (int frame = 0; frame < ANIM1_FRAMES; ++frame)
    {
        uint8_t allLocked = 1;
        HAL_Sprite_Clear();
        for (int r = 0; r < UI_EN_MAX_LINES; ++r)
        {
            char rowBuf[512];
            int bufIdx = 0;
            int currentW = 0;
            if (r < drawLines)
            {
                while (layout.lines[r][currentW] != '\0' && currentW < UI_EN_MAX_COLS - 1)
                {
                    if (frame >= lucky[r][currentW]) rowBuf[bufIdx++] = layout.lines[r][currentW];
                    else
                    {
                        allLocked = 0;
                        rowBuf[bufIdx++] = 33 + random(94);
                    }
                    ++currentW;
                }
            }
            while (currentW < UI_EN_MAX_COLS - 1)
            {
                if (frame < lucky[r][currentW])
                {
                    allLocked = 0;
                    rowBuf[bufIdx++] = 33 + random(94);
                }
                else rowBuf[bufIdx++] = ' ';
                ++currentW;
            }
            rowBuf[bufIdx] = '\0';
            if (bufIdx > 0) drawLineEn(r, rowBuf);
        }
        HAL_Screen_Update();
        if (!allLocked) procedure(cb);
        if (allLocked) break;
        delay(ANIM1_DELAY);
        yield();
    }
}

} // namespace

void InitGlitchPool()
{
    int totalChars = strlen(GLOBAL_CHINESE_DICT) / 3;
    for (int i = 0; i < CACHE_SIZE; ++i)
    {
        int pick = random(totalChars) * 3;
        g_glitchPool[i][0] = GLOBAL_CHINESE_DICT[pick];
        g_glitchPool[i][1] = GLOBAL_CHINESE_DICT[pick + 1];
        g_glitchPool[i][2] = GLOBAL_CHINESE_DICT[pick + 2];
        g_glitchPool[i][3] = '\0';
    }
}

void PrepareLayoutFromRule(const char* rule, SystemLang_t lang, uint16_t color, TextLayout& out)
{
    static char raw[1024];
    static char formatted[2048];

    if (!rule) rule = "";
    snprintf(raw, sizeof(raw), "_%s_", rule);
    raw[sizeof(raw) - 1] = '\0';

    out.lang = lang;
    out.color = color;
    if (lang == LANG_ZH) formatChineseToGrid(raw, formatted);
    else formatEnglishToGrid(raw, formatted);
    out.actualLines = splitToLines(formatted, out.lines);
}

void DrawChaosFrame(SystemLang_t lang, uint16_t color)
{
    HAL_Sprite_Clear();
    if (lang == LANG_ZH)
    {
        for (int row = 0; row < UI_CH_MAX_LINES; ++row)
        {
            char rowBuf[256];
            int bufIdx = 0;
            int currentW = 0;
            while (currentW < UI_CH_MAX_COLS - 1)
            {
                if (random(100) < 40 && currentW <= UI_CH_MAX_COLS - 2)
                {
                    int pick = random(CACHE_SIZE);
                    rowBuf[bufIdx++] = g_glitchPool[pick][0];
                    rowBuf[bufIdx++] = g_glitchPool[pick][1];
                    rowBuf[bufIdx++] = g_glitchPool[pick][2];
                    currentW += 2;
                }
                else
                {
                    rowBuf[bufIdx++] = 33 + random(94);
                    currentW += 1;
                }
            }
            rowBuf[bufIdx] = '\0';
            HAL_Screen_ShowChineseLine_Color(UI_CH_MARGIN_X, UI_CH_START_Y + row * UI_CH_ROW_HEIGHT, rowBuf, color);
        }
    }
    else
    {
        for (int row = 0; row < UI_EN_MAX_LINES; ++row)
        {
            char rowBuf[UI_EN_MAX_COLS + 1];
            for (int i = 0; i < UI_EN_MAX_COLS; ++i) rowBuf[i] = 33 + random(94);
            rowBuf[UI_EN_MAX_COLS] = '\0';
            HAL_Screen_ShowTextLine(UI_EN_MARGIN_X, UI_EN_START_Y + row * UI_EN_ROW_HEIGHT, rowBuf);
        }
    }
    HAL_Screen_Update();
}

void DrawDoneFrame(const TextLayout& layout, int scrollOffset)
{
    HAL_Sprite_Clear();
    int maxVis = MaxVisibleLines(layout.lang);
    int startY = (layout.lang == LANG_ZH) ? UI_CH_START_Y : UI_EN_START_Y;
    int rowH = (layout.lang == LANG_ZH) ? UI_CH_ROW_HEIGHT : UI_EN_ROW_HEIGHT;
    int marginX = (layout.lang == LANG_ZH) ? UI_CH_MARGIN_X : UI_EN_MARGIN_X;
    int drawCount = (layout.actualLines - scrollOffset > maxVis) ? maxVis : (layout.actualLines - scrollOffset);

    for (int i = 0; i < drawCount; ++i)
    {
        int r = scrollOffset + i;
        if (layout.lang == LANG_ZH) HAL_Screen_ShowChineseLine_Color(marginX, startY + i * rowH, layout.lines[r], layout.color);
        else HAL_Screen_ShowTextLine(marginX, startY + i * rowH, layout.lines[r]);
    }

    if (layout.actualLines > maxVis)
    {
        int maxOffset = layout.actualLines - maxVis;
        int trackH = maxVis * rowH;
        int barH = trackH * maxVis / layout.actualLines;
        if (barH < 4) barH = 4;
        int barY = startY + (trackH - barH) * scrollOffset / maxOffset;
        HAL_Fill_Rect(280, barY, 2, barH, 1);
    }
    HAL_Screen_Update();
}

void PlayDecodeSequence(TextLayout& layout, int decodeStyle, ProcedureTick cb)
{
    int drawLines = layout.actualLines;
    int maxVis = MaxVisibleLines(layout.lang);
    if (drawLines > maxVis) drawLines = maxVis;

    if (layout.lang == LANG_ZH)
    {
        if (decodeStyle == 1) playCursorZh(layout, drawLines, cb);
        else if (decodeStyle == 2) playGlitchOneByOneZh(layout, drawLines, cb);
        else if (decodeStyle == 3) playGlobalWaveZh(layout, drawLines, cb);
        else playMatrixLockZh(layout, drawLines, cb);
    }
    else
    {
        if (decodeStyle == 1) playCursorEn(layout, drawLines, cb);
        else if (decodeStyle == 2) playGlitchOneByOneEn(layout, drawLines, cb);
        else if (decodeStyle == 3) playGlobalWaveEn(layout, drawLines, cb);
        else playMatrixLockEn(layout, drawLines, cb);
    }
}

int MaxVisibleLines(SystemLang_t lang)
{
    return (lang == LANG_ZH) ? UI_CH_MAX_LINES : UI_EN_MAX_LINES;
}

} // namespace UIPrescript
