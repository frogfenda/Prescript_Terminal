/*
【模块职责】指令解码渲染实现。

AppPrescript 只负责“抽取哪条指令、什么时候进入 CHAOS/DECODE/DONE 状态”；
本文件负责“这条指令在 428×142 大屏上怎么排版、怎么乱码、怎么逐步解码、完成态怎么滚动”。

本轮大屏适配重点：
1. 不再使用旧 284×76 时代的固定行数、固定列数和 x=280 滚动条；
2. 每次绘制前根据 HAL_Get_Screen_Width/Height 和当前字体行高动态计算排版；
3. 四种解码动画全部保留：矩阵锁定、光标逐字、单字故障、全局波动；
4. 中英文都统一走 HAL 的 U8g2 字体管线，避免英文仍使用旧 6×8 小字；
5. 所有新增串口保持中文，本文件当前不新增常态串口输出，避免动画阶段刷屏。
*/
#include "ui_prescript_decoder.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "../hal/hal.h"

namespace UIPrescript {

namespace {

// 四种阻塞式解码动画的原有节奏参数。这里保留节奏，只把坐标和行列改为响应式。
static const int ANIM1_FRAMES = 22;
static const int ANIM1_DELAY = 26;
static const int ANIM2_SPEED_DELAY = 30;
static const int ANIM3_GLITCH_COUNT = 4;
static const int ANIM3_GLITCH_DELAY = 20;
static const int ANIM4_BASE_FRAMES = 6;
static const int ANIM4_DELAY = 20;

static const int CACHE_SIZE = 300;
static char g_glitchPool[CACHE_SIZE][4];

// 大屏下仍然保留原来的“系统异常/指令损坏”风格中文乱码池。
const char* GLOBAL_CHINESE_DICT = "数据错误系统异常致命警告内存溢出未知指令核心损坏参数丢失连接中断身份拒绝权限不足矩阵终端序列重载覆写剥离重构解析编译代码节点网关漏洞端口渗透代理溯源封锁拦截劫持";

// 动态布局仍然需要固定上限，避免栈数组随屏幕尺寸无限增长。
static const int MAX_RENDER_LINES = TextLayout::MaxLines;
static const int MAX_GRID_COLS = 96;
static const int CHAOS_ROW_BUFFER_BYTES = 512;

struct DecoderMetrics
{
    int screenW;
    int screenH;
    int marginX;
    int topY;
    int rowH;
    int maxLines;
    int textW;        // 正文换行安全宽度：专门防止行尾中文字形被裁切
    int chaosTextW;   // CHAOS 乱码填充宽度：保持视觉铺满，不跟随正文安全余量缩小
    int rightGuard;
    int cellW;
    int maxCols;
    int scrollbarX;
    int scrollbarW;
};

int utf8Len(const char* s)
{
    return UIText::Utf8CharLen((unsigned char)*s);
}

void procedure(ProcedureTick cb)
{
    if (cb) cb();
}

/**
 * 计算指令页当前布局。
 *
 * 这一步是大屏适配的核心：
 * - 不直接写死 4 行 / 45 列；
 * - 使用当前字体行高决定能显示多少行；
 * - 使用当前字体的 cellWidth 估算乱码矩阵列数；
 * - 滚动条永远贴近当前逻辑画布右边缘，而不是固定 x=280。
 */
DecoderMetrics metrics(SystemLang_t lang)
{
    DecoderMetrics m;
    m.screenW = HAL_Get_Screen_Width();
    m.screenH = HAL_Get_Screen_Height();

    /*
     * 指令抽取页使用 428×142 全屏画布。
     * 旧布局的上下边距和行距偏保守，换到大屏后只能显示少量行，
     * 乱码矩阵和最终指令都会集中在屏幕上半部。这里改成指令页专用的紧凑排版：
     * - 左右边距只保留必要安全距离；
     * - 顶部从更靠上位置开始；
     * - 行距按字体 baseline 推导，充分利用高度；
     * - 底部只保留字形下沿和滚动条所需空间。
     */
    m.marginX = max(8, m.screenW / 54);
    m.topY = max(6, m.screenH / 24);

    int fontBaseline = HAL_Get_Font_Baseline(HAL_FONT_BODY);
    int fontLineH = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
    m.rowH = max(fontBaseline + 2, fontLineH - 4);
    if (m.rowH < fontBaseline + 1)
        m.rowH = fontBaseline + 1;

    int bottomReserve = max(4, m.screenH / 36);
    int usableH = m.screenH - m.topY - bottomReserve;
    m.maxLines = usableH / m.rowH;
    if (m.maxLines < 1) m.maxLines = 1;
    if (m.maxLines > MAX_RENDER_LINES) m.maxLines = MAX_RENDER_LINES;

    m.scrollbarW = max(3, m.screenW / 140);
    m.scrollbarX = m.screenW - m.scrollbarW - 2;

    /*
     * 中文行尾偶发“最后一个字显示不全”的根因不是 UTF-8 被截断，
     * 而是 U8g2 的 getUTF8Width() 返回的是 advance 宽度，部分中文字形右侧
     * 点阵会比 advance 多出 1~数个像素。这里给正文右侧统一留出一个字形安全余量，
     * 让换行提前发生，避免文字贴到滚动条或 Sprite 右边界后被裁掉。
     */
    int rawTextW = m.scrollbarX - m.marginX - 6;
    if (rawTextW < 40) rawTextW = m.screenW - m.marginX * 2;

    /*
     * 正文换行和 CHAOS 乱码必须分开处理：
     * - 正文只保留少量右侧 guard，用来吸收 U8g2 advance width 与真实点阵边界的 1~数像素误差；
     * - CHAOS 使用完整 rawTextW 填充，避免为了正文安全边距把乱码区域压窄。
     *
     * 之前把 guard 做到接近一个字宽，中文不会裁切但视觉上右侧空白太大；
     * 这里改成小像素 guard，真正的防截断交给“整行候选测宽”处理。
     */
    int fontCell = 16;
#ifdef TERMINAL_FONT_CELL_WIDTH
    fontCell = TERMINAL_FONT_CELL_WIDTH;
#endif
    m.rightGuard = max(4, min(6, fontCell / 3 + 1));
    m.textW = rawTextW - m.rightGuard;
    if (m.textW < 40) m.textW = rawTextW;
    m.chaosTextW = rawTextW;

    /*
     * maxCols 只保留给矩阵锁定动画和 CHAOS 背景使用。
     * 注意这里使用 chaosTextW，而不是正文 textW，避免为了防裁切而把乱码区一起压窄。
     */
    m.cellW = max(6, fontCell / 2);
    m.maxCols = m.chaosTextW / m.cellW;

    if (lang == LANG_EN)
        m.maxCols = min(m.maxCols + 8, MAX_GRID_COLS - 1);
    else
        m.maxCols = min(m.maxCols, MAX_GRID_COLS - 1);

    if (m.maxCols < 12) m.maxCols = 12;
    return m;
}

/**
 * 把格式化文本拆进 TextLayout 行缓存。
 * 每行最多 255 字节，超过部分被截断；实际项目指令文本远小于这个上限。
 */
int splitToLines(const char* formatted, char lines[][256])
{
    int lineIdx = 0;
    int bufIdx = 0;
    for (int i = 0; formatted[i] != '\0' && lineIdx < TextLayout::MaxLines; ++i)
    {
        if (formatted[i] == '\n')
        {
            lines[lineIdx][bufIdx] = '\0';
            ++lineIdx;
            bufIdx = 0;
        }
        else
        {
            if (bufIdx < 255) lines[lineIdx][bufIdx++] = formatted[i];
        }
    }

    if (lineIdx < TextLayout::MaxLines && (bufIdx > 0 || lineIdx == 0))
    {
        lines[lineIdx][bufIdx] = '\0';
        ++lineIdx;
    }
    return lineIdx;
}

/**
 * 估算一个 UTF-8 字符在排版网格中占几列。
 * 中文和其他多字节字符占 2 列，ASCII 占 1 列。这个规则和乱码矩阵保持一致。
 */
int gridWidthForChar(const char* s)
{
    int clen = utf8Len(s);
    return (clen > 1) ? 2 : 1;
}

/**
 * 把一个 UTF-8 字符复制到临时 token 中，并返回它的字节数。
 * token 主要用于 HAL_Get_Text_Width_Font() 测量当前字体下的真实像素宽度。
 */
int makeUtf8Token(const char* src, char* token, int tokenCap)
{
    int clen = utf8Len(src);
    if (clen >= tokenCap) clen = tokenCap - 1;
    for (int i = 0; i < clen; ++i) token[i] = src[i];
    token[clen] = '\0';
    return clen;
}

/**
 * 安全追加 token 到格式化输出缓冲区。
 * PrepareLayoutFromRule() 目前提供 2048 字节 formatted 缓冲区，
 * 这里统一做边界检查，避免长指令或英文长单词导致写越界。
 */
bool appendTokenToFormatted(char* out, int& outIdx, int outCap, const char* token, int tokenLen)
{
    if (outIdx + tokenLen >= outCap - 1)
        return false;

    for (int i = 0; i < tokenLen; ++i)
        out[outIdx++] = token[i];
    return true;
}

/**
 * 单字符测宽入口。
 * U8g2 对空格和个别符号可能返回 0，这会让换行引擎错误地认为还能继续追加；
 * 因此这里给异常宽度一个保底值，并把所有换行判断统一走这个函数。
 */
int measureTokenWidth(const char* token, int tokenLen)
{
    if (!token || tokenLen <= 0)
        return 0;

    int w = HAL_Get_Text_Width_Font(token, HAL_FONT_BODY);
    if (w <= 0)
        w = (token[0] == ' ') ? 5 : HAL_Get_Font_Baseline(HAL_FONT_BODY);
    return w;
}

/**
 * 判断当前行追加 token 后是否会越过安全正文宽度。
 * 这里使用“已经收紧后的 m.textW”，不要再让调用者自己额外减 padding，
 * 避免不同动画/完成态的换行策略不一致。
 */
bool wouldOverflowLine(int currentPixelW, int tokenW, int safeTextW)
{
    return currentPixelW > 0 && (currentPixelW + tokenW > safeTextW);
}

/** 帧间让出时间片，避免动画连续刷屏时饿死音频/NFC/BLE 等后台任务。 */
void delayFrame(int ms)
{
    if (ms > 0) delay(ms);
    yield();
}

/**
 * 中文指令换行。
 *
 * 这里使用真实像素宽度换行，而不是旧版“半角列数”换行。
 * 原来的网格算法依赖 TERMINAL_FONT_CELL_WIDTH 估算，换成自定义字体后会过早换行，
 * 导致 428px 宽屏右侧空出一大段。逐字测宽后，指令正文会尽量填满当前正文区域。
 */
void formatChineseToGrid(const char* raw, char* out)
{
    DecoderMetrics m = metrics(LANG_ZH);
    int outIdx = 0;
    const int outCap = 2048;

    /*
     * 中文换行不再简单累加“单字测宽”。
     * 实机上 U8g2 的单字 advance 宽度累加值，和整行真正绘制出来的边界会有细小误差，
     * 行尾刚好贴边时就会出现最后一个中文字右侧被裁。这里每追加一个 UTF-8 字符前，
     * 先把“当前行 + 新字符”组成候选整行，再用整行宽度判断是否换行。
     */
    char currentLine[512] = {0};
    int lineIdx = 0;

    for (int i = 0; raw[i] != '\0';)
    {
        if (raw[i] == '\n')
        {
            if (!appendTokenToFormatted(out, outIdx, outCap, "\n", 1)) break;
            currentLine[0] = '\0';
            lineIdx = 0;
            ++i;
            continue;
        }

        char token[5] = {0};
        int clen = makeUtf8Token(&raw[i], token, sizeof(token));

        char candidate[sizeof(currentLine)] = {0};
        int candIdx = 0;
        for (int k = 0; k < lineIdx && candIdx < (int)sizeof(candidate) - 1; ++k)
            candidate[candIdx++] = currentLine[k];
        for (int k = 0; k < clen && candIdx < (int)sizeof(candidate) - 1; ++k)
            candidate[candIdx++] = token[k];
        candidate[candIdx] = '\0';

        int candidateW = HAL_Get_Text_Width_Font(candidate, HAL_FONT_BODY);
        if (lineIdx > 0 && candidateW > m.textW)
        {
            if (!appendTokenToFormatted(out, outIdx, outCap, "\n", 1)) break;
            currentLine[0] = '\0';
            lineIdx = 0;
        }

        if (!appendTokenToFormatted(out, outIdx, outCap, token, clen)) break;
        if (lineIdx + clen < (int)sizeof(currentLine) - 1)
        {
            for (int k = 0; k < clen; ++k) currentLine[lineIdx++] = token[k];
            currentLine[lineIdx] = '\0';
        }
        i += clen;
    }

    out[outIdx] = '\0';
}


/**
 * 英文指令换行。
 *
 * 英文优先按单词换行，但判断依据同样改为真实像素宽度。
 * 如果某个单词自身超过一整行，再逐字符切断，避免长 token 撑出屏幕。
 */
void appendToCurrentLine(char* line, int& lineIdx, const char* token, int tokenLen)
{
    if (!line || !token || tokenLen <= 0)
        return;

    for (int i = 0; i < tokenLen && lineIdx < 511; ++i)
        line[lineIdx++] = token[i];
    line[lineIdx] = '\0';
}

bool candidateFitsLine(const char* candidate, int safeTextW)
{
    if (!candidate)
        return true;
    return HAL_Get_Text_Width_Font(candidate, HAL_FONT_BODY) <= safeTextW;
}

/**
 * 英文指令换行。
 *
 * 根因说明：之前的英文路径仍在使用“单词宽度 + 单字符宽度累加”。
 * U8g2 的整行 kerning/advance 与逐 token 累加会存在误差，16px 字体下误差更明显，
 * 所以会出现中文已安全、英文尾字母仍贴边被裁的情况。
 *
 * 这里和中文一样，所有换行判断都改成“当前行 + 待加入内容”的整行测宽：
 * - 普通英文按单词换行，不在行尾保留多余空格；
 * - 超长单词才退回逐 UTF-8 字符切分；
 * - 右侧只用 metrics() 提供的小 guard，不再牺牲一整个字宽。
 */
void formatEnglishToGrid(const char* raw, char* out)
{
    DecoderMetrics m = metrics(LANG_EN);
    int outIdx = 0;
    int i = 0;
    const int outCap = 2048;

    char currentLine[512] = {0};
    int lineIdx = 0;

    auto emitNewline = [&]() -> bool {
        if (!appendTokenToFormatted(out, outIdx, outCap, "\n", 1))
            return false;
        currentLine[0] = '\0';
        lineIdx = 0;
        return true;
    };

    auto appendToOutputAndLine = [&](const char* token, int tokenLen) -> bool {
        if (!appendTokenToFormatted(out, outIdx, outCap, token, tokenLen))
            return false;
        appendToCurrentLine(currentLine, lineIdx, token, tokenLen);
        return true;
    };

    while (raw[i] != '\0')
    {
        if (raw[i] == '\n')
        {
            if (!emitNewline()) break;
            ++i;
            continue;
        }

        // 合并连续空格；行首不输出空格，避免空格占用宽度后影响后续测宽。
        if (raw[i] == ' ')
        {
            ++i;
            continue;
        }

        char wordBuf[256] = {0};
        int wordIdx = 0;
        while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\n' && wordIdx < (int)sizeof(wordBuf) - 4)
        {
            int clen = utf8Len(&raw[i]);
            for (int b = 0; b < clen; ++b)
                wordBuf[wordIdx++] = raw[i + b];
            i += clen;
        }
        wordBuf[wordIdx] = '\0';
        if (wordIdx <= 0)
            continue;

        bool needSpace = lineIdx > 0;

        char candidate[sizeof(currentLine)] = {0};
        int candIdx = 0;
        for (int k = 0; k < lineIdx && candIdx < (int)sizeof(candidate) - 1; ++k)
            candidate[candIdx++] = currentLine[k];
        if (needSpace && candIdx < (int)sizeof(candidate) - 1)
            candidate[candIdx++] = ' ';
        for (int k = 0; k < wordIdx && candIdx < (int)sizeof(candidate) - 1; ++k)
            candidate[candIdx++] = wordBuf[k];
        candidate[candIdx] = '\0';

        if (lineIdx > 0 && !candidateFitsLine(candidate, m.textW))
        {
            if (!emitNewline()) break;
            needSpace = false;
        }

        // 单词本身能放进一行时，按单词整体输出，避免英文被无意义地拆字母。
        if (HAL_Get_Text_Width_Font(wordBuf, HAL_FONT_BODY) <= m.textW)
        {
            if (needSpace)
            {
                if (!appendToOutputAndLine(" ", 1)) break;
            }
            if (!appendToOutputAndLine(wordBuf, wordIdx)) break;
            continue;
        }

        // 超长单词：逐 UTF-8 字符切分，但每一步仍然用整行候选测宽。
        int wi = 0;
        while (wi < wordIdx)
        {
            char token[5] = {0};
            int clen = makeUtf8Token(&wordBuf[wi], token, sizeof(token));

            char charCandidate[sizeof(currentLine)] = {0};
            int charCandIdx = 0;
            for (int k = 0; k < lineIdx && charCandIdx < (int)sizeof(charCandidate) - 1; ++k)
                charCandidate[charCandIdx++] = currentLine[k];
            if (needSpace && charCandIdx < (int)sizeof(charCandidate) - 1)
                charCandidate[charCandIdx++] = ' ';
            for (int k = 0; k < clen && charCandIdx < (int)sizeof(charCandidate) - 1; ++k)
                charCandidate[charCandIdx++] = token[k];
            charCandidate[charCandIdx] = '\0';

            if (lineIdx > 0 && !candidateFitsLine(charCandidate, m.textW))
            {
                if (!emitNewline()) break;
                needSpace = false;
                continue;
            }

            if (needSpace)
            {
                if (!appendToOutputAndLine(" ", 1)) break;
                needSpace = false;
            }
            if (!appendToOutputAndLine(token, clen)) break;
            wi += clen;
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
        int i = 0;
        while (layout.lines[r][i] != '\0')
        {
            ++total;
            i += utf8Len(&layout.lines[r][i]);
        }
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

void drawLineFast(const DecoderMetrics& m, int row, const char* text, uint16_t color)
{
    HAL_Screen_ShowChineseLine_Color(m.marginX, m.topY + row * m.rowH, text, color);
}

void drawLine(int row, const char* text, const TextLayout& layout)
{
    DecoderMetrics m = metrics(layout.lang);
    drawLineFast(m, row, text, layout.color);
}

void drawLineWithColor(int row, const char* text, SystemLang_t lang, uint16_t color)
{
    DecoderMetrics m = metrics(lang);
    drawLineFast(m, row, text, color);
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

void playCursor(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    DecoderMetrics m = metrics(layout.lang);
    int totalChars = (layout.lang == LANG_ZH) ? countCharsZh(layout, drawLines) : countCharsEn(layout, drawLines);

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
                    drawLine(r, printBuf, layout);

                    // 光标动画：在当前待解码字符位置画一个短矩形，尺寸随当前字体行高放大。
                    int cursorX = m.marginX + HAL_Get_Text_Width(printBuf);
                    int cursorW = (clen > 1) ? max(12, m.cellW * 2) : max(7, m.cellW);
                    int cursorH = max(12, HAL_Get_Font_Line_Height(HAL_FONT_BODY) - 4);
                    HAL_Fill_Rect(cursorX, m.topY + r * m.rowH + 2, cursorW, cursorH, 1);
                }
                ++currentChar;
                i += clen;
            }
            if (bufIdx > 0 && currentChar <= frame)
            {
                printBuf[bufIdx] = '\0';
                drawLine(r, printBuf, layout);
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

void playGlitchOneByOne(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = (layout.lang == LANG_ZH) ? countCharsZh(layout, drawLines) : countCharsEn(layout, drawLines);
    for (int target = 0; target <= totalChars; ++target)
    {
        bool isSpace = (layout.lang == LANG_ZH)
            ? ((target < totalChars) ? isTargetSpaceZh(layout, drawLines, target) : false)
            : ((target < totalChars) ? isTargetSpaceEn(layout, drawLines, target) : false);

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
                        else appendGlitchForChar(printBuf, bufIdx, clen, layout.lang == LANG_ZH);
                    }
                    ++currentChar;
                    i += clen;
                }
                if (bufIdx > 0)
                {
                    printBuf[bufIdx] = '\0';
                    drawLine(r, printBuf, layout);
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

void playGlobalWave(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    int totalChars = (layout.lang == LANG_ZH) ? countCharsZh(layout, drawLines) : countCharsEn(layout, drawLines);
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
                    else appendGlitchForChar(printBuf, bufIdx, clen, layout.lang == LANG_ZH);
                }
                ++currentChar;
                i += clen;
            }
            if (bufIdx > 0)
            {
                printBuf[bufIdx] = '\0';
                drawLine(r, printBuf, layout);
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

void playMatrixLock(TextLayout& layout, int drawLines, ProcedureTick cb)
{
    DecoderMetrics m = metrics(layout.lang);
    uint8_t lucky[MAX_RENDER_LINES][MAX_GRID_COLS];
    for (int r = 0; r < m.maxLines; ++r)
        for (int c = 0; c < m.maxCols; ++c)
            lucky[r][c] = random(11) + 2;

    for (int frame = 0; frame < ANIM1_FRAMES; ++frame)
    {
        uint8_t allLocked = 1;
        HAL_Sprite_Clear();
        for (int r = 0; r < m.maxLines; ++r)
        {
            char rowBuf[512];
            int bufIdx = 0;
            int currentW = 0;
            int byteIdx = 0;

            if (r < drawLines)
            {
                while (layout.lines[r][byteIdx] != '\0' && currentW < m.maxCols - 1)
                {
                    int clen = utf8Len(&layout.lines[r][byteIdx]);
                    int cw = gridWidthForChar(&layout.lines[r][byteIdx]);
                    if (frame >= lucky[r][currentW])
                    {
                        for (int b = 0; b < clen; ++b) rowBuf[bufIdx++] = layout.lines[r][byteIdx + b];
                    }
                    else
                    {
                        allLocked = 0;
                        appendGlitchForChar(rowBuf, bufIdx, clen, layout.lang == LANG_ZH);
                    }
                    currentW += cw;
                    byteIdx += clen;
                }
            }

            // 未被真实文本占用的剩余列也参与矩阵锁定，维持“全屏信号收束”的效果。
            while (currentW < m.maxCols - 1 && bufIdx < (int)sizeof(rowBuf) - 4)
            {
                if (frame < lucky[r][currentW])
                {
                    allLocked = 0;
                    if (layout.lang == LANG_ZH && random(100) < 40 && currentW <= m.maxCols - 2)
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
            if (bufIdx > 0) drawLineFast(m, r, rowBuf, layout.color);
        }
        HAL_Screen_Update();
        if (!allLocked) procedure(cb);
        if (allLocked) break;
        delayFrame(ANIM1_DELAY);
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
    out.actualLines = 0;
    for (int i = 0; i < TextLayout::MaxLines; ++i) out.lines[i][0] = '\0';
    if (lang == LANG_ZH) formatChineseToGrid(raw, formatted);
    else formatEnglishToGrid(raw, formatted);
    out.actualLines = splitToLines(formatted, out.lines);
}

bool appendChaosTokenByGrid(char* rowBuf, int& bufIdx, int bufCap, int& gridW, const char* token, int tokenLen, int tokenGridW, int maxGridW)
{
    if (!token || tokenLen <= 0 || bufIdx + tokenLen >= bufCap)
        return false;
    if (gridW > 0 && gridW + tokenGridW > maxGridW)
        return false;

    for (int i = 0; i < tokenLen; ++i)
        rowBuf[bufIdx++] = token[i];
    gridW += tokenGridW;
    return true;
}

void buildChaosRowByGrid(char* rowBuf, int bufCap, SystemLang_t lang, int maxGridW)
{
    int bufIdx = 0;
    int gridW = 0;

    /*
     * CHAOS 卡顿的根因不是字体“错”，而是每帧绘制大量 U8g2 glyph。
     * 这里保留整屏乱码刷新方式，但行生成不再逐 token 调 U8g2 测宽，
     * 只按轻量网格填充，并控制中文乱码比例，避免生成阶段额外拖慢动画。
     */
    while (bufIdx < bufCap - 4)
    {
        // 乱码态保留少量中文“系统故障”质感，但不要让每行堆满中文大字模。
        if (lang == LANG_ZH && random(100) < 18 && gridW <= maxGridW - 2)
        {
            int pick = random(CACHE_SIZE);
            if (!appendChaosTokenByGrid(rowBuf, bufIdx, bufCap, gridW, g_glitchPool[pick], 3, 2, maxGridW))
                break;
        }
        else
        {
            char ascii[2] = {(char)(33 + random(94)), '\0'};
            if (!appendChaosTokenByGrid(rowBuf, bufIdx, bufCap, gridW, ascii, 1, 1, maxGridW))
                break;
        }
    }

    rowBuf[bufIdx] = '\0';
}

void DrawChaosFrame(SystemLang_t lang, uint16_t color)
{
    DecoderMetrics m = metrics(lang);

    /*
     * 按你的反馈，撤回“首帧铺满、后续局部扰动”的局部刷新方案。
     * 该方案会让 CHAOS 视觉节奏和原先不同，也可能因为局部区域推送/清除导致残影或不一致。
     * 当前恢复为每帧完整生成并刷新乱码屏；后续性能优化只从声音异步化、乱码字形缓存、
     * 或降低 glyph 绘制成本入手，不再用局部扰动替代整屏乱码感。
     */
    HAL_Sprite_Clear();

    for (int row = 0; row < m.maxLines; ++row)
    {
        char rowBuf[CHAOS_ROW_BUFFER_BYTES];
        buildChaosRowByGrid(rowBuf, sizeof(rowBuf), lang, m.maxCols);
        drawLineFast(m, row, rowBuf, color);
    }

    HAL_Screen_Update();
}

void DrawDoneFrame(const TextLayout& layout, int scrollOffset)
{
    DecoderMetrics m = metrics(layout.lang);
    HAL_Sprite_Clear();

    int maxVis = MaxVisibleLines(layout.lang);
    int remain = layout.actualLines - scrollOffset;
    if (remain < 0) remain = 0;
    int drawCount = (remain > maxVis) ? maxVis : remain;

    for (int i = 0; i < drawCount; ++i)
    {
        int r = scrollOffset + i;
        drawLineFast(m, i, layout.lines[r], layout.color);
    }

    if (layout.actualLines > maxVis)
    {
        int maxOffset = layout.actualLines - maxVis;
        int trackH = maxVis * m.rowH;
        int barH = trackH * maxVis / layout.actualLines;
        if (barH < 6) barH = 6;
        int barY = m.topY + (trackH - barH) * scrollOffset / maxOffset;

        // 完成态滚动条贴到当前画布右侧，而不是旧屏固定 x=280。
        HAL_Fill_Rect(m.scrollbarX, m.topY, m.scrollbarW, trackH, UITheme::COLOR_DARK);
        HAL_Fill_Rect(m.scrollbarX, barY, m.scrollbarW, barH, 1);
    }

    HAL_Screen_Update();
}

void PlayDecodeSequence(TextLayout& layout, int decodeStyle, ProcedureTick cb)
{
    int drawLines = layout.actualLines;
    int maxVis = MaxVisibleLines(layout.lang);
    if (drawLines > maxVis) drawLines = maxVis;

    if (decodeStyle == 1) playCursor(layout, drawLines, cb);
    else if (decodeStyle == 2) playGlitchOneByOne(layout, drawLines, cb);
    else if (decodeStyle == 3) playGlobalWave(layout, drawLines, cb);
    else playMatrixLock(layout, drawLines, cb);
}

int MaxVisibleLines(SystemLang_t lang)
{
    return metrics(lang).maxLines;
}

} // namespace UIPrescript
