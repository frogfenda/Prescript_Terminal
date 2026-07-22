#pragma once
#include <Arduino.h>
#include "lang/terminal_lang.h"

/*
【模块职责】指令解码渲染接口。

AppPrescript 只把指令文本、语言、主题颜色和动画模式交给本模块；
本模块负责把文本排版到当前逻辑屏幕，并绘制以下三类画面：
- CHAOS：尚未确认前的乱码矩阵；
- DECODE：四种可设置的解码动画；
- DONE：解码完成后的可滚动文本。

当前大屏分支不再使用旧 284×76 固定行列，行数、列数、滚动条位置都会根据 HAL 屏幕尺寸和字体参数计算。
*/
namespace UIPrescript {

using ProcedureTick = void (*)();

struct TextLayout
{
    static const int MaxLines = 20;

    // 每行最终排版后的 UTF-8 文本。动画阶段不重新拆行，保证完成态和解码态位置一致。
    char lines[MaxLines][256];

    // 每行最终文本的像素宽度与居中后的 x 坐标。
    // 这些值在 PrepareLayoutFromRule() 后一次性计算，避免动画每帧因为乱码宽度不同而左右抖动。
    int lineW[MaxLines] = {0};
    int lineX[MaxLines] = {0};

    // 短指令完成态/解码态使用垂直居中；长指令保持顶部起始并允许滚动。
    int contentStartY = 0;

    int actualLines = 0;
    SystemLang_t lang = LANG_ZH;
    uint16_t color = 1;
};

/**
 * 【接口说明】可叠加在任意 App 背景之上的分页非阻塞解码动画，每页最多显示两行。
 *
 * 调用方先用 PrepareLayoutFromRule() 完成 UTF-8 换行，再把当前页的 firstLine/lineCount 交给 begin()。
 * 第一行固定使用原单行的屏幕中线位置，第二行只向下增加一行，不按块高度向上重新居中。
 * update() 只推进状态，drawOverlay() 只绘制到当前 HAL Sprite；两者都不会清屏、推屏或 delay，
 * 因而海面、传感器采样和系统主循环可以在文字动画期间继续运行。
 *
 * 对象会复制当前页文本，不保留 TextLayout 指针；调用方随后重用或销毁原布局不会造成悬空引用。
 */
class DecodeOverlayAnimator
{
public:
    static constexpr int MaxPageLines = 2;

    /**
     * 启动一页排版内容；lineCount 会钳制为 1~2，并受 layout 剩余行数限制。
     * 非法 firstLine 会安全结束并保持空文本；decodeStyle 与系统现有 0~3 设置一致。
     */
    void begin(const TextLayout &layout, int firstLine, int lineCount,
               int decodeStyle, uint32_t nowMs = millis());

    /** 按 millis 时间推进至当前应有帧；返回 true 表示本次调用改变了动画画面。 */
    bool update(uint32_t nowMs);

    /** 绘制当前动画帧到已经存在的 Sprite 顶层；不清屏、不推屏。 */
    void drawOverlay() const;

    /** 立即显示完整目标行；用于用户在动画期间按键时先完成本句而不是误跳内容。 */
    void finishImmediately();

    bool isRunning() const { return running_; }
    bool isFinished() const { return active_ && !running_; }
    bool isActive() const { return active_; }

private:
    static constexpr int TEXT_BYTES = 256;
    static constexpr int FRAME_BYTES = 512;
    static constexpr int MAX_CHARACTERS_PER_LINE = 192;
    static constexpr int MAX_TOTAL_CHARACTERS = MAX_CHARACTERS_PER_LINE * MaxPageLines;

    char target_[MaxPageLines][TEXT_BYTES] = {};
    char frame_text_[MaxPageLines][FRAME_BYTES] = {};
    uint8_t matrix_lucky_[MAX_TOTAL_CHARACTERS] = {};
    SystemLang_t lang_ = LANG_ZH;
    uint16_t color_ = 1;
    int decode_style_ = 0;
    int line_count_ = 0;
    int total_chars_ = 0;
    int frame_index_ = 0;
    int line_x_[MaxPageLines] = {};
    int line_y_[MaxPageLines] = {};
    int cursor_line_ = 0;
    int cursor_x_ = 0;
    int cursor_width_ = 0;
    uint32_t next_frame_ms_ = 0;
    bool active_ = false;
    bool running_ = false;
    bool cursor_visible_ = false;

    uint32_t frameIntervalMs() const;
    int finalFrameIndex() const;
    void rebuildFrame();
};

/** 初始化中文乱码池，为 CHAOS 和解码动画生成“系统故障”字符。 */
void InitGlitchPool();

/** 把原始指令文本按当前屏幕宽度和字体配置排成多行。 */
void PrepareLayoutFromRule(const char* rule, SystemLang_t lang, uint16_t color, TextLayout& out);

/** 绘制确认前的乱码矩阵帧。AppPrescript 在 S_CHAOS 状态下每帧调用。 */
void DrawChaosFrame(SystemLang_t lang, uint16_t color);

/** 绘制完成态文本和右侧滚动条。scrollOffset 表示当前从第几行开始显示。 */
void DrawDoneFrame(const TextLayout& layout, int scrollOffset);

/** 播放四种阻塞式解码动画；procedureTick 用来维持 procedure.wav 循环声。 */
void PlayDecodeSequence(TextLayout& layout, int decodeStyle, ProcedureTick procedureTick);

/** 返回当前字体和屏幕下最多能显示多少行完成态文本。 */
int MaxVisibleLines(SystemLang_t lang);

} // namespace UIPrescript
