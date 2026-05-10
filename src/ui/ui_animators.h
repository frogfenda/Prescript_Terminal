/*
【模块职责】通用编辑动画组件。DialAnimator 负责数字/字符串弧形滚轮，TacticalLinkEngine 负责顶部流程节点滑动。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include "../hal/hal.h"
#include "ui_theme.h"
#include "ui_text.h"
#include "ui_frame.h"

// ==========================================
// 机械刻度盘动画引擎 (DialAnimator - 安全区护航版)
// ==========================================
class DialAnimator {
private:
    float offset = 0.0f; 
    uint32_t last_tick = 0; // 【新增】帧率锁

    // 【函数说明】返回 UTF-8 字符长度，刻度盘逐字符裁剪时用它避免中文被切开。
    int getCharLen(unsigned char c) {
        if ((c & 0x80) == 0) return 1;
        if ((c & 0xE0) == 0xC0) return 2;
        if ((c & 0xF0) == 0xE0) return 3;
        if ((c & 0xF8) == 0xF0) return 4;
        return 1;
    }

    // 【函数说明】逐字符绘制刻度盘文字，只有完全处在屏幕范围内的字符才会显示，并按 fade 衰减。
    void drawClippedFadedText(int x, int y, const char* text, float fade) {
        int sw = HAL_Get_Screen_Width();
        int cursor_x = x;
        int i = 0;
        
        while (text[i] != '\0') {
            int len = getCharLen(text[i]);
            char buf[5] = {0};
            for(int b=0; b<len; b++) buf[b] = text[i+b];
            int char_w = HAL_Get_Text_Width(buf);
            if (char_w <= 0 || buf[0] == ' ') char_w = 5;
            
            if (cursor_x >= 0 && cursor_x + char_w <= sw) {
                if (buf[0] != ' ') HAL_Screen_ShowChineseLine_Faded(cursor_x, y, buf, fade);
            }
            
            cursor_x += char_w;
            i += len;
            if (cursor_x > sw) break; 
        }
    }

public:
    // 【函数说明】把旋钮 delta 累加到 offset，下一帧 update 会让刻度盘从偏移位置回弹。
    void trigger(int delta) { offset += (float)delta; }

    bool update() {
        if (abs(offset) > 0.001f) {
            uint32_t now = millis();
            // 【核心修复】：锁定在约 60FPS (16ms一帧)，防止疯狂重绘撕裂画面
            if (now - last_tick >= UITheme::FRAME_FAST_MS) {
                last_tick = now;
                offset += (0.0f - offset) * 0.25f; 
                if (abs(offset) < 0.02f) offset = 0.0f;
                return true; // 只有够 16ms 且发生位移才允许重绘！
            }
        }
        return false;
    }

    // 【函数说明】绘制数字弧形滚轮：中心值清晰显示，两侧数字按 sin 曲线分布并淡出，中心区域用角框强调。
    void drawNumberDial(int center_y, int current_val, int min_val, int max_val, const char* suffix = "") {
        int sw = HAL_Get_Screen_Width();
        int cx = sw / 2;
        float R = 150.0f;          
        float angle_step = 0.22f;  

        // 【新增】：16像素绝对安全区，禁止任何字符靠近屏幕物理边缘！
        int safe_margin = UITheme::SAFE_MARGIN_X; 

        for (int i = -6; i <= 6; i++) {
            float item_offset = i + offset;
            float angle = item_offset * angle_step;

            if (fabs(angle) <= 1.2f) {
                int val = current_val + i;
                int range = max_val - min_val + 1;
                while (val < min_val) val += range;
                while (val > max_val) val -= range;

                int draw_x = cx + (int)(sin(angle) * R);
                char buf[10];
                sprintf(buf, "%02d", val);
                int tw = HAL_Get_Text_Width(buf);

                // 核心拦截：如果字符串有一丝一毫跨入了安全区，直接不画！干脆利落！
                if (draw_x - tw / 2 >= safe_margin && draw_x + tw / 2 <= sw - safe_margin) {
                    float fade = fabs(angle) / 1.1f; 
                    if (fade > 0.85f) fade = 0.85f; 
                    drawClippedFadedText(draw_x - tw / 2, center_y, buf, fade);
                }
            }
        }

        int bx1 = cx - 22;
        int bx2 = cx + 22;
        int top_y = center_y - 2;
        int bot_y = center_y + 14;
        
        HAL_Draw_Line(bx1, top_y, bx1 + 4, top_y, 1); HAL_Draw_Line(bx1, top_y, bx1, top_y + 4, 1);
        HAL_Draw_Line(bx1, bot_y, bx1 + 4, bot_y, 1); HAL_Draw_Line(bx1, bot_y, bx1, bot_y - 4, 1);
        HAL_Draw_Line(bx2, top_y, bx2 - 4, top_y, 1); HAL_Draw_Line(bx2, top_y, bx2, top_y + 4, 1);
        HAL_Draw_Line(bx2, bot_y, bx2 - 4, bot_y, 1); HAL_Draw_Line(bx2, bot_y, bx2, bot_y - 4, 1);
        
        if (strlen(suffix) > 0) {
            HAL_Screen_ShowChineseLine(cx + 30, center_y, suffix);
        }
    }
    
    // 【函数说明】绘制字符串弧形滚轮：用于月份、类型等离散选项，文字沿横向弧线滑动并在边缘裁剪。
    void drawStringDial(int center_y, int current_idx, const char** str_array, int max_count) {
        int sw = HAL_Get_Screen_Width();
        int cx = sw / 2;
        float R = 160.0f; 
        float angle_step = 0.40f; 
        int safe_margin = UITheme::SAFE_MARGIN_X;
        
        for (int i = -4; i <= 4; i++) {
            float item_offset = i + offset;
            float angle = item_offset * angle_step;

            if (fabs(angle) <= 1.2f) {
                int idx = current_idx + i;
                while (idx < 0) idx += max_count;
                while (idx >= max_count) idx -= max_count;

                int draw_x = cx + (int)(sin(angle) * R);
                int tw = HAL_Get_Text_Width(str_array[idx]);
                    
                // 字符串边界安全区拦截
                if (draw_x - tw / 2 >= safe_margin && draw_x + tw / 2 <= sw - safe_margin) {
                    float fade = fabs(angle) / 1.1f; 
                    if (fade > 0.85f) fade = 0.85f; 
                    drawClippedFadedText(draw_x - tw / 2, center_y, str_array[idx], fade);
                }
            }
        }
        
        int bx1 = cx - 42;
        int bx2 = cx + 42;
        int top_y = center_y - 2;
        int bot_y = center_y + 14;
        
        HAL_Draw_Line(bx1, top_y, bx1 + 4, top_y, 1); HAL_Draw_Line(bx1, top_y, bx1, top_y + 4, 1);
        HAL_Draw_Line(bx1, bot_y, bx1 + 4, bot_y, 1); HAL_Draw_Line(bx1, bot_y, bx1, bot_y - 4, 1);
        HAL_Draw_Line(bx2, top_y, bx2 - 4, top_y, 1); HAL_Draw_Line(bx2, top_y, bx2, top_y + 4, 1);
        HAL_Draw_Line(bx2, bot_y, bx2 - 4, bot_y, 1); HAL_Draw_Line(bx2, bot_y, bx2, bot_y - 4, 1);
    }
};

// ==========================================
// 战术链路动画引擎 (TacticalLinkEngine)
// ==========================================
class TacticalLinkEngine {
private:
    float offset = 0.0f; 
    uint32_t last_tick = 0; // 【新增】帧率锁


    // 【函数说明】返回 UTF-8 字符长度，刻度盘逐字符裁剪时用它避免中文被切开。
    int getCharLen(unsigned char c) {
        if ((c & 0x80) == 0) return 1;
        if ((c & 0xE0) == 0xC0) return 2;
        if ((c & 0xF0) == 0xE0) return 3;
        if ((c & 0xF8) == 0xF0) return 4;
        return 1;
    }

    // 【函数说明】测量一个 UTF-8 字符宽度，空格按固定宽度处理。
    int getCharWidth(const char* c_str, int len) {
        char buf[5] = {0};
        for(int b=0; b<len; b++) buf[b] = c_str[b];
        int cw = HAL_Get_Text_Width(buf);
        if (cw <= 0 || buf[0] == ' ') return 5; 
        return cw;
    }

    // 【函数说明】逐字符累加得到阶段标签宽度，用于流程节点居中。
    int getWordWidth(const char* text) {
        int total = 0;
        int i = 0;
        while (text[i] != '\0') {
            int len = getCharLen(text[i]);
            total += getCharWidth(&text[i], len);
            i += len;
        }
        return total;
    }

    // 【函数说明】逐字符绘制流程链路标签，只有完整落入屏幕的字符显示。
    void drawClippedText(int x, int y, const char* text) {
        int sw = HAL_Get_Screen_Width();
        int cursor_x = x;
        int i = 0;
        
        while (text[i] != '\0') {
            int len = getCharLen(text[i]);
            char buf[5] = {0};
            for(int b=0; b<len; b++) buf[b] = text[i+b];
            int char_w = getCharWidth(&text[i], len);
            
            if (cursor_x >= 0 && cursor_x + char_w <= sw) {
                if (len == 1 && buf[0] != ' ') HAL_Screen_ShowTextLine(cursor_x, y, buf);
                else if (buf[0] != ' ') HAL_Screen_ShowChineseLine(cursor_x, y, buf);
            }
            
            cursor_x += char_w;
            i += len;
            if (cursor_x > sw) break; 
        }
    }

public:
    // 【函数说明】让流程链路动画直接跳到指定阶段，进入编辑页时避免从 0 慢慢滑入。
    void jumpTo(int phase) { offset = (float)phase; }
    
    bool update(int current_phase) {
        if (abs(offset - current_phase) > 0.005f) {
            uint32_t now = millis();
            // 【核心修复】：60FPS 锁帧
            if (now - last_tick >= UITheme::FRAME_FAST_MS) {
                last_tick = now;
                offset += (current_phase - offset) * 0.15f; 
                if (abs(offset - current_phase) < 0.01f) offset = current_phase;
                return true;
            }
        }
        return false;
    }
    
    // 【函数说明】绘制顶部流程链路：当前阶段位于中心，前后阶段沿水平轴滑动，节点之间用短线连接。
    void draw(int y, const char** names, int count, int current_phase, int spacing = 120) {
        int sw = HAL_Get_Screen_Width();
        int cx = sw / 2;
        int text_y = y; 
        int line_y = y + 6; 

        int top_y = text_y - 2;
        int bot_y = text_y + 14;

        for (int i = 0; i < count; i++) {
            float dx = (i - offset) * spacing;
            int node_x = cx + (int)dx;
            
            int tw = getWordWidth(names[i]);
            drawClippedText(node_x - tw / 2, text_y, names[i]);
            
            if (i == current_phase) {
                int bx1 = node_x - tw / 2 - 4;
                int bx2 = node_x + tw / 2 + 4;
                
                if (bx1 > 0 && bx1 < sw) {
                    HAL_Draw_Line(bx1, top_y, bx1 + 3, top_y, 1);
                    HAL_Draw_Line(bx1, top_y, bx1, top_y + 3, 1);
                    HAL_Draw_Line(bx1, bot_y, bx1 + 3, bot_y, 1);
                    HAL_Draw_Line(bx1, bot_y, bx1, bot_y - 3, 1);
                }
                if (bx2 > 0 && bx2 < sw) {
                    HAL_Draw_Line(bx2, top_y, bx2 - 3, top_y, 1);
                    HAL_Draw_Line(bx2, top_y, bx2, top_y + 3, 1);
                    HAL_Draw_Line(bx2, bot_y, bx2 - 3, bot_y, 1);
                    HAL_Draw_Line(bx2, bot_y, bx2, bot_y - 3, 1);
                }
            }
            
            if (i < count - 1) {
                float next_dx = (i + 1 - offset) * spacing;
                int next_node_x = cx + (int)next_dx;
                int next_tw = HAL_Get_Text_Width(names[i+1]);
                
                int line_start = node_x + tw / 2 + (i == current_phase ? 8 : 4);
                int line_end = next_node_x - next_tw / 2 - (i + 1 == current_phase ? 8 : 4);
                
                if (line_end > line_start && line_start < sw && line_end > 0) {
                    if (i == current_phase || i + 1 == current_phase) {
                        HAL_Draw_Line(max(0, line_start), line_y - 1, min(sw, line_end), line_y - 1, 1);
                        HAL_Draw_Line(max(0, line_start), line_y + 1, min(sw, line_end), line_y + 1, 1);
                    } else {
                        for (int lx = line_start; lx < line_end; lx += 4) {
                            if (lx >= 0 && lx + 2 <= sw) {
                                HAL_Draw_Line(lx, line_y, lx + 2, line_y, 1);
                            }
                        }
                    }
                }
            }
        }
    }
};
