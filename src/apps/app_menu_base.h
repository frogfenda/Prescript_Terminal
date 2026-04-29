// 文件：src/apps/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "app_base.h"
#include "app_manager.h"
#include <time.h>
#include "sys_power.h"

class AppMenuBase : public AppBase
{
protected:
    int current_selection;
    float visual_selection;

    virtual int getMenuCount() = 0;
    virtual const char *getTitle() = 0;
    virtual const char *getItemText(int index) = 0;
    virtual void onItemClicked(int index) = 0;
    virtual void onLongPressed() = 0;
    uint32_t menu_anim_last_tick = 0;

    virtual bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix)
    {
        return false;
    }

    virtual uint16_t getItemColor(int index)
    {
        return TFT_CYAN;
    }

    void drawMenuUI(float v_pos)
    {
        HAL_Sprite_Clear();
        sysPower.drawBatteryIcon(4, 4);
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // ==========================================
        // 提取 UI 布局语义常数
        // ==========================================
        const int UI_PADDING_X = 10;
        const int UI_LINE_MARGIN_Y = 8;
        const int UI_ITEM_SPACING_Y = 24;
        const int UI_SCAN_BOX_PAD_X = 6;
        const int UI_SCAN_BOX_H = 24;
        const int UI_SCROLL_BAR_W = 5;

        // 3D 轮盘侧弯的曲率系数
        const float UI_3D_CURVE_FACTOR = 12.0f;

        // ==========================================
        // 1. 左侧：动态自适应 HUD 信息面板
        // ==========================================
        const char *title_text = getTitle();
        int title_w = HAL_Get_Text_Width(title_text);

        char time_str[10];
        SysTime_GetTimeString(time_str);
        int time_w = HAL_Get_Text_Width(time_str);

        int max_content_w = (title_w > time_w) ? title_w : time_w;
        int left_panel_w = max_content_w + (UI_PADDING_X * 2);

        int title_x = (left_panel_w - title_w) / 2;
        int title_y = (sh / 2) - 20;
        HAL_Screen_ShowChineseLine(title_x, title_y, title_text);

        int time_x = (left_panel_w - time_w) / 2;
        int time_y = (sh / 2) + 4;
        HAL_Screen_ShowTextLine(time_x, time_y, time_str);

        HAL_Draw_Line(left_panel_w, UI_LINE_MARGIN_Y, left_panel_w, sh - UI_LINE_MARGIN_Y, 1);

        // ==========================================
        // 【新增】：动态倒计时 HUD (支持 NFC 与 TMR 堆叠摆放)
        // ==========================================
        extern bool g_nfc_is_emulating;
        extern uint32_t g_nfc_emu_end_time;
        extern volatile bool g_countdown_active;
        extern uint32_t g_countdown_end_time;

        int hud_y_offset = sh - 14; // 从左下角开始往上堆叠

        if (g_nfc_is_emulating)
        {
            uint32_t now = millis();
            int remain_sec = 0;
            if (g_nfc_emu_end_time > now)
                remain_sec = (g_nfc_emu_end_time - now) / 1000;

            char bus_str[16];
            sprintf(bus_str, "[BUS %02d]", remain_sec);
            int bus_w = HAL_Get_Text_Width(bus_str);
            HAL_Screen_ShowTextLine((left_panel_w - bus_w) / 2, hud_y_offset, bus_str);
            hud_y_offset -= 14; // 把坐标往上顶，给下一个留位置
        }

        if (g_countdown_active)
        {
            uint32_t now = millis();
            int remain_sec = 0;
            if (g_countdown_end_time > now)
                remain_sec = (g_countdown_end_time - now) / 1000;

            char tmr_str[16];
            sprintf(tmr_str, "[TMR %02d:%02d]", remain_sec / 60, remain_sec % 60);
            int tmr_w = HAL_Get_Text_Width(tmr_str);
            HAL_Screen_ShowTextLine((left_panel_w - tmr_w) / 2, hud_y_offset, tmr_str);
        }
        // ==========================================
        // 2. 右侧：3D 滚轴核心阵列 (方向与视觉反转版)
        // ==========================================
        int right_panel_x = left_panel_w;
        int right_panel_w = sw - left_panel_w;

        // 中心点轻微向右偏移，给左侧的方块留出余地
        int center_x = right_panel_x + (right_panel_w / 2) + 6;
        int center_y = sh / 2;

        int real_count = getMenuCount();
        if (real_count == 0)
            return;
        int count = (real_count == 2) ? 4 : real_count;

        int box_x = right_panel_x + UI_SCAN_BOX_PAD_X;
        int box_y = center_y - (UI_SCAN_BOX_H / 2);
        int box_w = right_panel_w - (UI_SCAN_BOX_PAD_X * 2);
        HAL_Draw_Rect(box_x, box_y, box_w, UI_SCAN_BOX_H, 1);

        // 【修改 1：把三角形移到右侧，并且朝左指】
        int tri_x = box_x + box_w - 6;
        int tri_size = 5;
        // 原本是向右(tri_x + tri_size + 1)，现在改成向左(tri_x - tri_size - 1)
        HAL_Fill_Triangle(tri_x, center_y - tri_size, tri_x, center_y + tri_size, tri_x - tri_size - 1, center_y, 1);

        int idx1 = (int)floor(v_pos);
        int idx2 = idx1 + 1;
        float fraction = v_pos - idx1;

        int logical_idx1 = (idx1 % count + count) % count;
        int logical_idx2 = (idx2 % count + count) % count;
        int real_idx1 = logical_idx1 % real_count;
        int real_idx2 = logical_idx2 % real_count;

        int w1 = HAL_Get_Text_Width(getItemText(real_idx1));
        int w2 = HAL_Get_Text_Width(getItemText(real_idx2));
        float dynamic_text_w = w1 + (w2 - w1) * fraction;

        // 【修改 2：把动态尾随方块移到文字的左边】
        // 向左偏移文本一半的宽度，再减去边距和方块自身的厚度
        int block_x = center_x - (int)(dynamic_text_w / 2.0f) - UI_PADDING_X - UI_SCROLL_BAR_W;
        int min_block_x = right_panel_x + UI_SCAN_BOX_PAD_X + 4; // 防止方块撞击左侧装甲线
        if (block_x < min_block_x)
            block_x = min_block_x;

        int block_y = center_y - (UI_SCAN_BOX_H / 2) + 2;
        int block_h = UI_SCAN_BOX_H - 4;
        HAL_Fill_Rect(block_x, block_y, UI_SCROLL_BAR_W, block_h, 1);

        int base_idx = round(v_pos);

        for (int i = base_idx - 2; i <= base_idx + 2; i++)
        {
            float offset = i - v_pos;
            int item_y = center_y + (int)(offset * UI_ITEM_SPACING_Y);

            if (item_y < -UI_ITEM_SPACING_Y || item_y > sh + UI_ITEM_SPACING_Y)
                continue;

            int logical_idx = (i % count + count) % count;
            int real_idx = logical_idx % real_count;
            const char *text = getItemText(real_idx);

            int text_width = HAL_Get_Text_Width(text);
            int base_x = center_x - (text_width / 2);

            // 【修改 3：3D 侧弯反向】
            // 将减号改成加号，当菜单滚出中间区域时，会向屏幕右侧“飞出”，视觉上更舒展
            int item_x = base_x + (int)(offset * offset * UI_3D_CURVE_FACTOR);

            // 防止极致侧弯时字母撞破右侧屏幕边缘
            if (item_x + text_width > sw - 5)
                item_x = sw - text_width - 5;

            float distance = abs(offset);
            int final_y = item_y - (UI_ITEM_SPACING_Y / 3);

            const char *p_pref = nullptr, *p_val = nullptr, *p_suff = nullptr;

            if (logical_idx == current_selection && getItemEditParts(real_idx, &p_pref, &p_val, &p_suff))
            {
                drawSegmentedAnimatedText(item_x, final_y, p_pref, p_val, p_suff, distance);
            }
            else
            {
                uint16_t item_color = getItemColor(real_idx);
                HAL_Screen_ShowChineseLine_Faded_Color(item_x, final_y, text, distance, item_color);
            }
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        current_selection = 0;
        visual_selection = 0.0f;
        onResume();
    }

    void onResume() override { drawMenuUI(visual_selection); }
    void onBackground() override {}

    void onLoop() override
    {
        bool needs_redraw = updateEditAnimation();

        int real_count = getMenuCount();
        if (real_count == 0)
            return;
        int count = (real_count == 2) ? 4 : real_count;

        float target = (float)current_selection;
        float diff = target - visual_selection;

        if (diff > count / 2.0f)
            diff -= count;
        if (diff < -count / 2.0f)
            diff += count;

        if (abs(diff) > 0.01f)
        {
            uint32_t now = millis();
            // 【核心修复】：将主菜单的滑动也锁定在 60FPS
            if (now - menu_anim_last_tick >= 16)
            {
                menu_anim_last_tick = now;
                visual_selection += diff * 0.25f;
                while (visual_selection < 0)
                    visual_selection += count;
                while (visual_selection >= count)
                    visual_selection -= count;
                needs_redraw = true;
            }
        }
        else if (visual_selection != target && abs(diff) <= 0.01f)
        {
            visual_selection = target;
            needs_redraw = true;
        }

        // ==========================================
        // 【核心新增】：HUD 倒计时自动心跳刷新引擎 (NFC + TMR)
        // ==========================================
        extern bool g_nfc_is_emulating;
        extern uint32_t g_nfc_emu_end_time;
        extern volatile bool g_countdown_active;
        extern uint32_t g_countdown_end_time;

        static int last_nfc_sec = -1;
        static int last_tmr_sec = -1;

        uint32_t now = millis();

        // --- NFC 刷新判定 ---
        if (g_nfc_is_emulating)
        {
            int remain_sec = 0;
            if (g_nfc_emu_end_time > now)
                remain_sec = (g_nfc_emu_end_time - now) / 1000;
            if (remain_sec != last_nfc_sec)
            {
                last_nfc_sec = remain_sec;
                needs_redraw = true;
            }
        }
        else if (last_nfc_sec != -1)
        {
            last_nfc_sec = -1;
            needs_redraw = true;
        }

        // --- 普通倒计时刷新判定 ---
        if (g_countdown_active)
        {
            int remain_sec = 0;
            if (g_countdown_end_time > now)
                remain_sec = (g_countdown_end_time - now) / 1000;
            if (remain_sec != last_tmr_sec)
            {
                last_tmr_sec = remain_sec;
                needs_redraw = true;
            }
        }
        else if (last_tmr_sec != -1)
        {
            last_tmr_sec = -1;
            needs_redraw = true;
        }
        extern bool HAL_Power_NeedsRedraw();

        if (sysPower.needsRedraw())
        {
            needs_redraw = true;
        }
        // ==========================================
        // 最终统一推屏
        if (needs_redraw)
        {
            drawMenuUI(visual_selection);
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override
    {
        int real_count = getMenuCount();
        if (real_count > 0)
        {
            int count = (real_count == 2) ? 4 : real_count;
            // 【修改 4：菜单旋转反向】
            // 这里将 `+ delta` 改成了 `- delta`，从此顺时针与逆时针的操作逻辑完全互换！
            current_selection = (current_selection + delta + count) % count;
            SYS_SOUND_GLITCH();
        }
    }

    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        int real_count = getMenuCount();
        if (real_count > 0)
        {
            onItemClicked(current_selection % real_count);
        }
    }

    void onKeyLong() override { onLongPressed(); }
};

#endif