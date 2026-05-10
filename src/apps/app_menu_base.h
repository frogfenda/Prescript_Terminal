/*
【模块职责】通用滚轮菜单基类。子类只提供标题、条目文本和点击动作，本类统一绘制左侧 HUD、右侧 3D 滚轮、选中框、尾随方块和旋钮滑动插值。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "app_base.h"
#include "app_manager.h"
#include <time.h>
#include "sys_power.h"
#include "../ui/ui_hud.h"
#include "../ui/ui_theme.h"

class AppMenuBase : public AppBase
{
protected:
    int current_selection;
    float visual_selection;

    // 【接口说明】子类返回菜单真实条目数，AppMenuBase 用它处理循环滚动和点击取模。
    virtual int getMenuCount() = 0;
    virtual const char *getTitle() = 0;
    virtual const char *getItemText(int index) = 0;
    // 【接口说明】子类处理当前条目的短按确认动作。
    virtual void onItemClicked(int index) = 0;
    virtual void onLongPressed() = 0;
    uint32_t menu_anim_last_tick = 0;

    // 【函数说明】子类可把条目拆成前缀、动态值、后缀，让当前项的数值部分拥有跳动动画。
    virtual bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix)
    {
        return false;
    }

    // 【函数说明】子类可为条目提供颜色，默认青色。
    virtual uint16_t getItemColor(int index)
    {
        return TFT_CYAN;
    }

    // 【函数说明】绘制完整菜单页：先画左侧 HUD，再画右侧选中框、指针、动态方块和 5 个弧形滚轮条目。
    void drawMenuUI(float v_pos)
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // 共享菜单布局常数集中在 UITheme，避免菜单视觉参数散落。
        constexpr int UI_PADDING_X = UITheme::Menu::PaddingX;
        constexpr int UI_ITEM_SPACING_Y = UITheme::Menu::ItemSpacingY;
        constexpr int UI_SCAN_BOX_PAD_X = UITheme::Menu::ScanBoxPadX;
        constexpr int UI_SCAN_BOX_H = UITheme::Menu::ScanBoxHeight;
        constexpr int UI_SCROLL_BAR_W = UITheme::Menu::ScrollBarWidth;
        constexpr float UI_3D_CURVE_FACTOR = UITheme::Menu::CurveFactor;

        // ==========================================
        // 1. 左侧 HUD 面板
        // ==========================================
        int left_panel_w = UIHud_DrawLeftPanel(getTitle());
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
    // 【函数说明】菜单首次进入时把选中项和视觉位置归零，并立刻绘制菜单。
    void onCreate() override
    {
        current_selection = 0;
        visual_selection = 0.0f;
        onResume();
    }

    // 【接口说明】从子页面返回时按当前 visual_selection 重绘菜单。
    void onResume() override { drawMenuUI(visual_selection); }
    void onBackground() override {}

    // 【函数说明】推进菜单滑动插值、编辑值跳动动画，并在 HUD/电量变化时重绘页面。
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
            if (now - menu_anim_last_tick >= UITheme::FRAME_FAST_MS)
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

        // HUD 的 NFC / TT2 / 时间刷新由运行状态聚合层判断，AppMenuBase 不再直接 extern 其他模块全局变量。
        if (UIHud_NeedsRedraw())
        {
            needs_redraw = true;
        }

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

    // 【函数说明】菜单销毁时不释放资源，静态 App 实例保留状态。
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

    // 【函数说明】短按确认当前条目，先播放确认反馈再调用子类 onItemClicked。
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        int real_count = getMenuCount();
        if (real_count > 0)
        {
            onItemClicked(current_selection % real_count);
        }
    }

    // 【接口说明】长按交给子类 onLongPressed，实现返回、保存退出等页面规则。
    void onKeyLong() override { onLongPressed(); }
};

#endif
