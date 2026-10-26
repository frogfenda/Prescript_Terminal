/*
【模块职责】菜单渲染器注册表、滚轮渲染器和运行时帧耗时统计。
【分层边界】渲染器只依赖 UIMenuRenderSource 视图接口，不依赖 AppMenuBase 或具体页面类型。
*/
#include "ui/ui_menu_renderer.h"

#include "hal/hal.h"
#include "ui/ui_frame.h"
#include "ui/ui_hud.h"
#include "ui/ui_menu_motion.h"
#include "ui/ui_paper_menu.h"
#include "ui/ui_text.h"
#include "ui/ui_theme.h"

namespace
{
    UIWheelMenuRenderer g_wheel_menu_renderer;
    UIPaperMenuRenderer g_paper_menu_renderer;
    UIMenuRenderStyle g_menu_style = UIMenuRenderStyle::Wheel;
    UIMenuFrameTiming g_frame_timing;
    uint32_t g_last_frame_start_us = 0;

    int PositiveMod(int value, int modulo)
    {
        if (modulo <= 0)
            return 0;
        const int result = value % modulo;
        return result < 0 ? result + modulo : result;
    }

    float MenuVerticalOffset(float offset, int spacing_y, float sling_strength)
    {
        const float distance = fabsf(offset);
        if (distance <= 0.001f)
            return 0.0f;
        const float sign = offset >= 0.0f ? 1.0f : -1.0f;
        const float first_gap = 1.14f + sling_strength * 0.08f;
        const float outer_gap = 0.74f + sling_strength * 0.06f;
        const float units = distance <= 1.0f
            ? distance * first_gap
            : first_gap + (distance - 1.0f) * outer_gap;
        return sign * (float)spacing_y * units * UITheme::Menu::ArcSpread;
    }

    float MenuCurveOffsetX(float offset, float curve_factor)
    {
        const float distance = fabsf(offset);
        if (distance <= 0.001f)
            return 0.0f;
        return powf(min(distance, 2.7f), 1.62f) * curve_factor;
    }

    float MenuSlingOffsetX(float offset, float sling_strength)
    {
        const float distance = fabsf(offset);
        if (distance <= 0.05f || sling_strength <= 0.01f)
            return 0.0f;
        const float d = min(distance, 3.0f);
        const float speed_curve = powf(d, 1.55f) * UITheme::Menu::CurveSpeedBoost();
        const float speed_sling = powf(d, 1.12f) * UITheme::Menu::SlingSpeedBoost();
        return -(speed_curve + speed_sling) * sling_strength;
    }

    int MenuTextLeft(float offset, int text_width, int center_x, int right_panel_x,
                     int screen_width, float curve_factor, float sling)
    {
        const int x = center_x - text_width / 2 +
                      (int)(MenuCurveOffsetX(offset, curve_factor) + MenuSlingOffsetX(offset, sling));
        return max(right_panel_x + 2, min(screen_width - text_width - 5, x));
    }

    void DrawWrappedMenuText(int x, int y, const UIMenuTextLayout &layout,
                             float distance, uint16_t color)
    {
        const int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        for (uint8_t i = 0; i < layout.line_count; ++i)
        {
            const int line_w = layout.line_widths[i] > 0
                ? layout.line_widths[i]
                : HAL_Get_Text_Width_Font(layout.lines[i].c_str(), HAL_FONT_BODY);
            const int line_x = x + (layout.width - line_w) / 2;
            HAL_Screen_ShowChineseLine_Faded_Color(line_x, y + i * (line_h + 2),
                                                   layout.lines[i].c_str(), distance, color);
        }
    }
}

void UIWheelMenuRenderer::draw(UIMenuRenderSource &menu, float v_pos,
                               const SysHudStatus &hud_status)
{
    const uint32_t frame_start_us = micros();
    HAL_Sprite_Clear();
    const int sw = HAL_Get_Screen_Width();
    const int sh = HAL_Get_Screen_Height();
    const int left_panel_w = UIHud_DrawLeftPanel(menu.getMenuRenderTitle(), hud_status);
    const int right_panel_x = left_panel_w;
    const int right_panel_w = sw - left_panel_w;

    const int real_count = menu.getMenuRenderCount();
    if (real_count == 0)
        return;
    menu.ensureMenuRenderState(real_count);
    const int count = real_count == 2 ? 4 : real_count;

    const int center_y = sh / 2;
    const int box_x = right_panel_x + UITheme::Menu::ScanBoxPadX();
    const int box_w = right_panel_w - UITheme::Menu::ScanBoxPadX() * 2;
    const int text_max_w = max(24, box_w - UITheme::Menu::PaddingX() * 2 -
                                    UITheme::Menu::ScrollBarWidth() - 18);
    const float curve_factor = UITheme::Menu::CurveFactor();

    UIMenuTextLayout frame_layouts[24];
    bool frame_layout_valid[24] = {};
    auto frameLayout = [&](int index, UIMenuTextLayout &fallback) -> const UIMenuTextLayout * {
        if (index >= 0 && index < 24 && frame_layout_valid[index])
            return &frame_layouts[index];
        UIMenuTextLayout *out = (index >= 0 && index < 24) ? &frame_layouts[index] : &fallback;
        if (!menu.getMenuRenderLayout(index, text_max_w, *out))
            *out = UIMenuTextLayout();
        if (index >= 0 && index < 24)
            frame_layout_valid[index] = true;
        return out;
    };

    const int display_selection = PositiveMod((int)roundf(v_pos), real_count);
    UIMenuTextLayout selected_fallback;
    const UIMenuTextLayout *selected_layout = frameLayout(display_selection, selected_fallback);

    const bool settled_now = fabsf(v_pos - menu.getMenuRenderTargetPosition()) < 0.015f &&
                             fabsf(menu.getMenuRenderVelocity()) < 0.15f;
    if (!settled_now)
        menu.setMenuRenderSettled(false);
    else if (!menu.getMenuRenderSettled())
    {
        menu.setMenuRenderSettled(true);
        menu.setMenuRenderSettledSince(millis());
    }

    const bool expand = selected_layout->line_count > 1 && menu.getMenuRenderSettled() &&
                        millis() - menu.getMenuRenderSettledSince() >= UITheme::Menu::ExpandDelayMs;
    float expansion = menu.getMenuRenderExpansion();
    float box_height = menu.getMenuRenderBoxHeight();
    float label_width = menu.getMenuRenderLabelWidth();
    bool layout_animating = UI_MenuApproach(expand ? 1.0f : 0.0f,
                                            menu.getMenuRenderFrameDt(),
                                            UITheme::Menu::LayoutResponse, expansion);
    if (selected_layout->line_count > 1 && menu.getMenuRenderSettled() && !expand)
        layout_animating = true;

    const float extra_height = max(0.0f, (float)selected_layout->height -
                                      HAL_Get_Font_Line_Height(HAL_FONT_BODY));
    const float target_box_h = min((float)(sh - 18), max((float)UITheme::Menu::ScanBoxHeight(),
        HAL_Get_Font_Line_Height(HAL_FONT_BODY) + extra_height * expansion + 8.0f));
    const float target_label_w = selected_layout->line_widths[0] +
        (selected_layout->width - selected_layout->line_widths[0]) * expansion;
    if (box_height == 0.0f)
    {
        box_height = target_box_h;
        label_width = target_label_w;
    }
    layout_animating |= UI_MenuApproach(target_box_h, menu.getMenuRenderFrameDt(),
                                        UITheme::Menu::LayoutResponse, box_height);
    layout_animating |= UI_MenuApproach(target_label_w, menu.getMenuRenderFrameDt(),
                                        UITheme::Menu::LayoutResponse, label_width);
    layout_animating |= box_height != target_box_h || label_width != target_label_w;
    menu.setMenuRenderLayout(expansion, box_height, label_width, layout_animating);

    const float active_sling = menu.getMenuRenderSlingDisplay();
    const int selected_text_w_for_pos = max(12, (int)lroundf(label_width));
    const int rest_center_x = right_panel_x + right_panel_w / 2 - UITheme::Menu::CenterRestOffsetX();
    const int speed_center_x = rest_center_x - (int)(active_sling * UITheme::Menu::CenterFlingX());
    const int min_center_x = right_panel_x + UITheme::Menu::CenterMinLeftGap() + selected_text_w_for_pos / 2;
    const int max_center_x = sw - selected_text_w_for_pos / 2 - 12;
    int center_x = max(min_center_x, speed_center_x);
    if (max_center_x >= min_center_x && center_x > max_center_x)
        center_x = max_center_x;

    const int dynamic_box_h = (int)lroundf(box_height);
    const int item_spacing_y = max(UITheme::Menu::ItemSpacingY(), (dynamic_box_h * 3) / 4);
    const int box_y = center_y - dynamic_box_h / 2;

    if (real_count > 1)
    {
        UIMenuRopeAnchor anchors[9];
        const int base = (int)roundf(v_pos);
        UIMenuRopes &ropes = menu.menuRenderRopes();
        for (int k = 0; k < 9; ++k)
        {
            const int slot = base - 4 + k;
            const float offset = slot - v_pos;
            const int index = PositiveMod(slot, count) % real_count;
            UIMenuTextLayout fallback;
            const UIMenuTextLayout *layout = frameLayout(index, fallback);
            const float rope_offset = offset + ropes.followOffset();
            const int rope_x = MenuTextLeft(rope_offset, layout->width, center_x,
                                            right_panel_x, sw, curve_factor, active_sling);
            const float rope_y = center_y + MenuVerticalOffset(rope_offset, item_spacing_y, active_sling);
            anchors[k] = {rope_x + layout->width * 0.5f, rope_y, (float)slot};
        }
        ropes.draw(anchors, 9, v_pos, right_panel_x + 2, sw - 5);
    }

    UIFrame::DrawFloatingSelection(box_x, box_y, box_w, dynamic_box_h,
                                   menu.getMenuRenderReflection());
    const int tri_x = box_x + box_w - 6;
    const int tri_size = constrain(dynamic_box_h / 3, 5, 11);
    HAL_Fill_Triangle(tri_x, center_y - tri_size, tri_x, center_y + tri_size,
                      tri_x - tri_size - 1, center_y, 1);

    const int block_x = max(right_panel_x + UITheme::Menu::ScanBoxPadX() + 4,
                            center_x - (int)(label_width / 2.0f) - UITheme::Menu::PaddingX() -
                            UITheme::Menu::ScrollBarWidth());
    HAL_Fill_Rect(block_x, center_y - dynamic_box_h / 2 + 2,
                  UITheme::Menu::ScrollBarWidth(), dynamic_box_h - 4, 1);

    const int base_idx = (int)roundf(v_pos);
    const int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
    for (int i = base_idx - 3; i <= base_idx + 3; ++i)
    {
        const float offset = (float)i - v_pos;
        const float distance = fabsf(offset);
        const int item_y = center_y + (int)MenuVerticalOffset(offset, item_spacing_y, active_sling);
        if (item_y < -item_spacing_y || item_y > sh + item_spacing_y)
            continue;

        const int logical_idx = PositiveMod(i, count);
        const int real_idx = logical_idx % real_count;
        UIMenuTextLayout fallback;
        const UIMenuTextLayout *item_layout = frameLayout(real_idx, fallback);
        const bool is_center_slot = distance < 0.55f;
        const float focus = max(0.0f, 1.0f - distance);
        const float focus_smooth = focus * focus * (3.0f - 2.0f * focus);
        const float item_expansion = expansion * focus_smooth;
        const int text_width = item_layout->width;
        const int item_x = MenuTextLeft(offset, text_width, center_x, right_panel_x, sw,
                                        curve_factor, active_sling);
        const char *prefix = nullptr, *value = nullptr, *suffix = nullptr;
        if (is_center_slot && real_idx == display_selection &&
            menu.getMenuRenderItemEditParts(real_idx, &prefix, &value, &suffix))
        {
            const int edit_w = HAL_Get_Text_Width(prefix ? prefix : "") +
                               HAL_Get_Text_Width(value ? value : "") +
                               HAL_Get_Text_Width(suffix ? suffix : "");
            const int edit_x = MenuTextLeft(offset, edit_w, center_x, right_panel_x, sw,
                                            curve_factor, active_sling);
            menu.drawMenuRenderEditText(edit_x, item_y - line_h / 2,
                                        prefix, value, suffix, distance);
        }
        else
        {
            const uint16_t item_color = menu.getMenuRenderItemColor(real_idx);
            const int first_y = item_y - line_h / 2 -
                                (int)lroundf((line_h + 2) * item_expansion * 0.5f);
            if (item_layout->line_count > 0)
                HAL_Screen_ShowChineseLine_Faded_Color(
                    item_x + (text_width - item_layout->line_widths[0]) / 2,
                    first_y, item_layout->lines[0].c_str(), distance, item_color);
            if (item_layout->line_count > 1 && item_expansion > 0.002f)
            {
                const uint16_t second_color = (uint16_t)(
                    ((uint16_t)(((item_color >> 11) & 31) * item_expansion) << 11) |
                    ((uint16_t)(((item_color >> 5) & 63) * item_expansion) << 5) |
                    (uint16_t)((item_color & 31) * item_expansion));
                HAL_Screen_ShowChineseLine_Faded_Color(
                    item_x + (text_width - item_layout->line_widths[1]) / 2,
                    first_y + line_h + 2, item_layout->lines[1].c_str(),
                    distance, second_color);
            }
        }
    }

    const uint32_t push_start_us = micros();
    HAL_Screen_Update();
    UI_RecordMenuFrameTiming(frame_start_us, push_start_us - frame_start_us,
                             micros() - push_start_us);
    menu.resetMenuRenderFrameDt();
}

UIMenuRenderer &UI_MenuRendererForStyle(UIMenuRenderStyle style)
{
    if (style == UIMenuRenderStyle::PaperTape)
        return g_paper_menu_renderer;
    return g_wheel_menu_renderer;
}

UIMenuRenderStyle UI_GetMenuRenderStyle() { return g_menu_style; }
void UI_SetMenuRenderStyle(UIMenuRenderStyle style)
{
    g_menu_style = style == UIMenuRenderStyle::PaperTape ? style : UIMenuRenderStyle::Wheel;
}

UIMenuFrameTiming UI_GetMenuFrameTiming() { return g_frame_timing; }
void UI_RecordMenuFrameTiming(uint32_t frame_start_us, uint32_t draw_us, uint32_t push_us)
{
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
