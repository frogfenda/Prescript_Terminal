/*
【模块职责】菜单渲染器注册表、滚轮兼容渲染器和纸带渲染器的样式选择。

第一阶段保留 AppMenuBase 原有绘制实现，先把“页面请求绘制”和“具体样式选择”
分开。这样新增第二套菜单时可以直接增加一个独立 UIMenuRenderer，而不必改动
旋钮、按键、陀螺仪事件和菜单条目业务代码。
*/
#include "ui/ui_menu_renderer.h"
#include "apps/app_menu_base.h"
#include "ui/ui_paper_menu.h"

namespace
{
    UIWheelMenuRenderer g_wheel_menu_renderer;
    UIPaperMenuRenderer g_paper_menu_renderer;
    UIMenuRenderStyle g_menu_style = UIMenuRenderStyle::Wheel;
    UIMenuFrameTiming g_frame_timing;
    uint32_t g_last_frame_start_us = 0;
}

void AppMenuBase::drawWheelMenuUI(float v_pos)
    {
        const uint32_t frame_start_us = micros();
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        // 共享菜单布局常数集中在 UITheme，避免菜单视觉参数散落。
        const int UI_PADDING_X = UITheme::Menu::PaddingX();
        const int UI_BASE_ITEM_SPACING_Y = UITheme::Menu::ItemSpacingY();
        const int UI_SCAN_BOX_PAD_X = UITheme::Menu::ScanBoxPadX();
        const int UI_BASE_SCAN_BOX_H = UITheme::Menu::ScanBoxHeight();
        const int UI_SCROLL_BAR_W = UITheme::Menu::ScrollBarWidth();
        // 静态曲率只负责基础 3D 右退；速度产生的额外位移交给 menuSlingOffsetX() 统一向左处理。
        const float UI_3D_CURVE_FACTOR = UITheme::Menu::CurveFactor();

        // ==========================================
        // 1. 左侧 HUD 面板
        // ==========================================
        int left_panel_w = UIHud_DrawLeftPanel(getTitle());
        // ==========================================
        // 2. 右侧：3D 滚轴核心阵列
        // ==========================================
        int right_panel_x = left_panel_w;
        int right_panel_w = sw - left_panel_w;

        int real_count = getMenuCount();
        if (real_count == 0)
            return;
        ensureMenuRuntimeState(real_count);
        int count = virtualMenuCount(real_count);

        int center_y = sh / 2;

        int box_x = right_panel_x + UI_SCAN_BOX_PAD_X;
        int box_w = right_panel_w - (UI_SCAN_BOX_PAD_X * 2);
        int text_max_w = max(24, box_w - UI_PADDING_X * 2 - UI_SCROLL_BAR_W - 18);

        int display_selection = positiveMod((int)roundf(v_pos), real_count);
        prepareMenuLayoutCache(real_count, text_max_w);

        // 绳子九个隐形控制点与可见文字经常引用同一条目；当帧只获取/核对一次排版。
        // 借用现有共享缓存的稳定条目，不保留 getItemText() 可能复用的临时字符串指针。
        // 仅在本次 draw 内有效：下帧重新读取动态内容；超过缓存上限的条目仍走原有 fallback。
        const WrappedMenuText *frame_layouts[MENU_LAYOUT_CACHE_MAX] = {};
        auto frameLayout = [&](int index, WrappedMenuText &fallback) -> const WrappedMenuText * {
            const bool cacheable = index >= 0 && index < MENU_LAYOUT_CACHE_MAX;
            if (cacheable && frame_layouts[index])
                return frame_layouts[index];
            const auto *layout = getCachedWrappedMenuText(index, getItemText(index), text_max_w, fallback);
            if (cacheable)
                frame_layouts[index] = layout;
            return layout;
        };

        WrappedMenuText selected_fallback;
        const WrappedMenuText *selected_layout = frameLayout(display_selection, selected_fallback);

        // 长文字所有槽位复用同一两行排版，滚动时只展示第一行；停稳 80ms 后才展开第二行。
        // 第一行不在 roundf(v_pos) 跨槽时更换截断方式，避免滚动半程出现文字内容/宽度跳变。
        const bool settled = fabsf(v_pos - menu_target_position) < 0.015f && fabsf(menu_velocity) < 0.15f;
        if (!settled)
            menu_settled = false;
        else if (!menu_settled)
        {
            menu_settled = true;
            menu_settled_since = millis();
        }
        const bool expand = selected_layout->line_count > 1 && settled &&
                            millis() - menu_settled_since >= UITheme::Menu::ExpandDelayMs;
        menu_layout_animating = UI_MenuApproach(expand ? 1.0f : 0.0f, menu_frame_dt,
                                                UITheme::Menu::LayoutResponse, menu_expansion);
        if (selected_layout->line_count > 1 && settled && !expand)
            menu_layout_animating = true;
        const float extra_height = max(0, selected_layout->height - HAL_Get_Font_Line_Height(HAL_FONT_BODY));
        const float target_box_h = min((float)(sh - 18), max((float)UI_BASE_SCAN_BOX_H,
                                   HAL_Get_Font_Line_Height(HAL_FONT_BODY) + extra_height * menu_expansion + 8.0f));
        const float target_label_w = selected_layout->line_widths[0] +
                                    (selected_layout->width - selected_layout->line_widths[0]) * menu_expansion;
        if (menu_box_height == 0.0f)
        {
            menu_box_height = target_box_h;
            menu_label_width = target_label_w;
        }
        menu_layout_animating |= UI_MenuApproach(target_box_h, menu_frame_dt,
                                                 UITheme::Menu::LayoutResponse, menu_box_height);
        menu_layout_animating |= UI_MenuApproach(target_label_w, menu_frame_dt,
                                                 UITheme::Menu::LayoutResponse, menu_label_width);
        // 点击事件可在推进动画前强制绘制（dt=0）；尺寸已变化时仍登记后续动画帧。
        menu_layout_animating |= menu_box_height != target_box_h || menu_label_width != target_label_w;

        // 绘制使用平滑后的甩动强度，不直接使用瞬时旋钮速度。
        // 这样中心项和外圈条目的回弹会连续过渡，不会在停下瞬间跳回。
        float active_sling = menu_sling_display;

        // 静止布局：选中项在右侧区域中心略偏左，选中框保持原位；长文本仍受左右安全边界保护。
        // 动态布局：只有滚轮仍在追赶目标位置时，中心项才随速度向左产生弹性甩动；
        // 这样中心项不会一直固定在原地，也不会在停稳后残留偏移。
        int selected_text_w_for_pos = max(12, (int)lroundf(menu_label_width));
        int rest_center_x = right_panel_x + right_panel_w / 2 - UITheme::Menu::CenterRestOffsetX();
        int speed_center_x = rest_center_x - (int)(active_sling * UITheme::Menu::CenterFlingX());
        int min_center_x = right_panel_x + UITheme::Menu::CenterMinLeftGap() + selected_text_w_for_pos / 2;
        int max_center_x = sw - selected_text_w_for_pos / 2 - 12;
        int center_x = speed_center_x;
        if (center_x < min_center_x)
            center_x = min_center_x;
        if (max_center_x >= min_center_x && center_x > max_center_x)
            center_x = max_center_x;

        int dynamic_box_h = (int)lroundf(menu_box_height);
        // 长选项允许截断/半露出，不再为了完整显示每一项把间距拉得很大。
        // 两行选中项只轻微撑开滚轮，保证一屏能看到更多条目。
        int item_spacing_y = max(UI_BASE_ITEM_SPACING_Y, (dynamic_box_h * 3) / 4);

        int box_y = center_y - (dynamic_box_h / 2);
        if (real_count > 1)
        {
            // 控制点隐形，屏外各预留一个；绳子穿过条目中心附近，再由上层框和透明字形覆盖。
            UIMenuRopeAnchor anchors[9];
            const int base = (int)roundf(v_pos);
            for (int k = 0; k < 9; ++k)
            {
                const int slot = base - 4 + k;
                const float offset = slot - v_pos;
                const int index = positiveMod(slot, count) % real_count;
                WrappedMenuText fallback;
                const auto *layout = frameLayout(index, fallback);
                // 滞后应用在控制点几何上，不能只改变曲线采样下标，否则整条绳子的轮廓仍会锁死文字。
                const float rope_offset = offset + menu_ropes.followOffset();
                const int rope_x = menuTextLeft(rope_offset, layout->width, center_x, right_panel_x, sw,
                                                UI_3D_CURVE_FACTOR, active_sling);
                const float rope_y = center_y + menuVerticalOffset(rope_offset, item_spacing_y, active_sling);
                // 使用安全区夹取后的实际文字中心，长条目靠边时也能从正文中部穿过。
                // 不裁掉整块文字矩形；HAL 字体已有透明背景，只让字形笔画自然挡住绳子。
                anchors[k] = {rope_x + layout->width * 0.5f, rope_y, (float)slot};
            }
            menu_ropes.draw(anchors, 9, v_pos, right_panel_x + 2, sw - 5);
        }
        UIFrame::DrawFloatingSelection(box_x, box_y, box_w, dynamic_box_h, menu_reflection);

        // 指针高度跟随选中框，长选项换成两行时不再显得太小。
        int tri_x = box_x + box_w - 6;
        int tri_size = constrain(dynamic_box_h / 3, 5, 11);
        HAL_Fill_Triangle(tri_x, center_y - tri_size, tri_x, center_y + tri_size, tri_x - tri_size - 1, center_y, 1);

        float dynamic_text_w = menu_label_width;

        // 动态尾随方块放在文字左边，尺寸跟随选中框。
        int block_x = center_x - (int)(dynamic_text_w / 2.0f) - UI_PADDING_X - UI_SCROLL_BAR_W;
        int min_block_x = right_panel_x + UI_SCAN_BOX_PAD_X + 4;
        if (block_x < min_block_x)
            block_x = min_block_x;

        int block_y = center_y - (dynamic_box_h / 2) + 2;
        int block_h = dynamic_box_h - 4;
        HAL_Fill_Rect(block_x, block_y, UI_SCROLL_BAR_W, block_h, 1);

        int base_idx = (int)roundf(v_pos);
        int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);

        for (int i = base_idx - 3; i <= base_idx + 3; i++)
        {
            float offset = (float)i - v_pos;
            float distance = fabsf(offset);

            // 纵向位置使用分段映射：第一圈稍微离开中心，外圈逐步压缩。
            // 这样快速滚动时不会出现“中间挤、边缘突然拉远”的节奏断层。
            float perspective_y = menuVerticalOffset(offset, item_spacing_y, active_sling);
            int item_y = center_y + (int)perspective_y;

            if (item_y < -item_spacing_y || item_y > sh + item_spacing_y)
                continue;

            int logical_idx = positiveMod(i, count);
            int real_idx = logical_idx % real_count;

            WrappedMenuText item_fallback;
            const WrappedMenuText *item_layout = nullptr;
            bool is_center_slot = distance < 0.55f;
            if (is_center_slot && real_idx == display_selection)
                item_layout = selected_layout;
            else
                item_layout = frameLayout(real_idx, item_fallback);

            // 展开权重随槽位距离连续变化，而不是在中心槽门限处突然切换两行。
            float focus = max(0.0f, 1.0f - distance);
            focus = focus * focus * (3.0f - 2.0f * focus);
            const float expansion = menu_expansion * focus;
            int text_width = item_layout->width;
            int item_x = menuTextLeft(offset, text_width, center_x, right_panel_x, sw,
                                      UI_3D_CURVE_FACTOR, active_sling);

            const char *p_pref = nullptr, *p_val = nullptr, *p_suff = nullptr;

            if (is_center_slot && real_idx == display_selection && getItemEditParts(real_idx, &p_pref, &p_val, &p_suff))
            {
                int edit_w = HAL_Get_Text_Width(p_pref ? p_pref : "") +
                             HAL_Get_Text_Width(p_val ? p_val : "") +
                             HAL_Get_Text_Width(p_suff ? p_suff : "");
                int edit_x = menuTextLeft(offset, edit_w, center_x, right_panel_x, sw,
                                          UI_3D_CURVE_FACTOR, active_sling);
                drawSegmentedAnimatedText(edit_x, item_y - line_h / 2, p_pref, p_val, p_suff, distance);
            }
            else
            {
                uint16_t item_color = getItemColor(real_idx);
                const int first_y = item_y - line_h / 2 - (int)lroundf((line_h + 2) * expansion * 0.5f);
                if (item_layout->line_count > 0)
                    HAL_Screen_ShowChineseLine_Faded_Color(item_x + (text_width - item_layout->line_widths[0]) / 2,
                                                          first_y, item_layout->lines[0].c_str(), distance, item_color);
                if (item_layout->line_count > 1 && expansion > 0.002f)
                {
                    // 直接调 RGB565 强度可完全淡出；现有 distance 接口有 15% 亮度下限。
                    const uint16_t second_color = (uint16_t)(
                        ((uint16_t)(((item_color >> 11) & 31) * expansion) << 11) |
                        ((uint16_t)(((item_color >> 5) & 63) * expansion) << 5) |
                        (uint16_t)((item_color & 31) * expansion));
                    HAL_Screen_ShowChineseLine_Faded_Color(item_x + (text_width - item_layout->line_widths[1]) / 2,
                        first_y + line_h + 2, item_layout->lines[1].c_str(), distance, second_color);
                }
            }
        }

        const uint32_t push_start_us = micros();
        HAL_Screen_Update();
        UI_RecordMenuFrameTiming(frame_start_us, push_start_us - frame_start_us, micros() - push_start_us);
        // 时间只消费一次；同一事件内额外强制重绘不重复推进布局。
        menu_frame_dt = 0.0f;
    }

UIMenuRenderer &UI_MenuRendererForStyle(UIMenuRenderStyle style)
{
    switch (style)
    {
    case UIMenuRenderStyle::PaperTape:
        return g_paper_menu_renderer;
    case UIMenuRenderStyle::Wheel:
    default:
        return g_wheel_menu_renderer;
    }
}

UIMenuRenderStyle UI_GetMenuRenderStyle() { return g_menu_style; }
void UI_SetMenuRenderStyle(UIMenuRenderStyle style)
{
    // 未注册值回退到滚轮，不让页面使用不完整的动画契约。
    g_menu_style = style == UIMenuRenderStyle::PaperTape ? style : UIMenuRenderStyle::Wheel;
}

void UIWheelMenuRenderer::draw(AppMenuBase &menu, float visual_position)
{
    // 当前滚轮布局仍由 AppMenuBase 的兼容实现提供，确保本阶段画面和操作完全不变。
    // 后续渲染器可以直接读取同一菜单页面状态，实现全新的整屏布局。
    menu.drawWheelMenuUI(visual_position);
}

UIMenuFrameTiming UI_GetMenuFrameTiming() { return g_frame_timing; }
void UI_RecordMenuFrameTiming(uint32_t frame_start_us, uint32_t draw_us, uint32_t push_us)
{
    // 单一主循环使用；记录整数耗时，不申请内存，也不在每帧输出日志。
    const uint32_t interval = frame_start_us - g_last_frame_start_us;
    g_frame_timing.interval_us = g_frame_timing.frames && interval <= 250000U ? interval : 0;
    g_last_frame_start_us = frame_start_us;
    ++g_frame_timing.frames;
    g_frame_timing.draw_us = draw_us;
    g_frame_timing.push_us = push_us;
    g_frame_timing.max_draw_us = max(g_frame_timing.max_draw_us, draw_us);
    g_frame_timing.max_push_us = max(g_frame_timing.max_push_us, push_us);
    g_frame_timing.max_interval_us = max(g_frame_timing.max_interval_us, g_frame_timing.interval_us);
}
