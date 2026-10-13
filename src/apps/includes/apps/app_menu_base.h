/*
【模块职责】通用菜单控制基类。子类只提供标题、条目文本和点击动作，本类统一管理输入、选中索引、编辑状态和页面生命周期；具体整屏菜单外观通过 UIMenuRenderer 选择。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_menu_base.h
#ifndef __APP_MENU_BASE_H
#define __APP_MENU_BASE_H

#include "sys/app_base.h"
#include "sys/app_manager.h"
#include <math.h>
#include <time.h>
#include "sys/sys_power.h"
#include "ui/ui_hud.h"
#include "ui/ui_theme.h"
#include "ui/ui_menu_renderer.h"
#include "ui/ui_menu_motion.h"
#include "ui/ui_clock.h"

class AppMenuBase : public AppBase
{
    friend class UIWheelMenuRenderer;

protected:
    // current_selection 始终保存“真实条目索引”。
    // 对 2 项菜单，滚轮内部仍会扩展成 4 个虚拟槽位保证循环感，但业务层只看到 0/1。
    int current_selection;
    float visual_selection;

    // 旋钮快速连续输入时不能只保存取模后的索引，否则 visual_selection 会在 0/count 边界反复找最短路跳动。
    // menu_target_position 使用未取模的连续坐标，绘制时再映射到真实条目。
    int menu_target_position = 0;

    // 【接口说明】子类返回菜单真实条目数，AppMenuBase 用它处理循环滚动和点击取模。
    virtual int getMenuCount() = 0;
    virtual const char *getTitle() = 0;
    virtual const char *getItemText(int index) = 0;
    // 【接口说明】子类处理当前条目的短按确认动作。
    virtual void onItemClicked(int index) = 0;
    virtual void onLongPressed() = 0;
    uint32_t menu_anim_last_tick = 0;
    UIFrameGate menu_frame_gate;
    bool menu_redraw_pending = false;
    float menu_velocity = 0.0f;
    float menu_motion_position = 0.0f;
    float menu_frame_dt = 0.0f;
    // 展开系数和框尺寸是页面视觉状态；只在同一帧更新，不绑定整数选中槽的切换瞬间。
    float menu_expansion = 0.0f;
    float menu_box_height = 0.0f;
    float menu_label_width = 0.0f;
    uint32_t menu_settled_since = 0;
    bool menu_settled = false;
    bool menu_layout_animating = false;

    // 曲率动效由旋钮输入速度驱动：慢速时保持稳定可读，快速旋转时临时增强 3D 侧弯。
    // 这里保存的是 0.0~1.0 的能量值，在 onLoop 中随时间衰减。
    float menu_curve_energy = 0.0f;
    // menu_sling_display 是真正用于绘制的甩动强度。
    // 它不会直接等于旋钮瞬时速度，而是平滑追踪目标值，避免快速滚动结束时中心项突然跳回。
    float menu_sling_display = 0.0f;
    uint32_t menu_last_knob_tick = 0;
    uint32_t menu_curve_last_tick = 0;
    uint32_t menu_sling_last_tick = 0;

    struct WrappedMenuText
    {
        String lines[2];
        int line_widths[2] = {0, 0};
        uint8_t line_count = 0;
        int width = 0;
        int height = 0;
    };

    static constexpr int MENU_LAYOUT_CACHE_MAX = 24;

    struct MenuLayoutCacheEntry
    {
        bool valid = false;
        bool compact_valid = false;
        bool wrapped_valid = false;
        int max_width = 0;
        String text;
        WrappedMenuText compact;
        WrappedMenuText wrapped;
    };

    static MenuLayoutCacheEntry *sharedMenuLayoutCache()
    {
        static MenuLayoutCacheEntry cache[MENU_LAYOUT_CACHE_MAX];
        return cache;
    }

    static int &sharedMenuLayoutCacheRealCount()
    {
        static int real_count = -1;
        return real_count;
    }

    static int &sharedMenuLayoutCacheTextMaxWidth()
    {
        static int text_max_width = -1;
        return text_max_width;
    }

    static int positiveMod(int value, int modulo)
    {
        if (modulo <= 0)
            return 0;
        int result = value % modulo;
        return (result < 0) ? (result + modulo) : result;
    }

    int virtualMenuCount(int real_count) const
    {
        return (real_count == 2) ? 4 : real_count;
    }

    int nearestVirtualPositionForRealIndex(float reference, int real_index, int real_count, int virtual_count) const
    {
        if (real_count <= 0 || virtual_count <= 0)
            return 0;

        real_index = positiveMod(real_index, real_count);
        int base = (int)roundf(reference);
        int best = real_index;
        float best_dist = 1000000.0f;

        // 在 reference 附近找一个映射到同一真实条目的虚拟槽位，避免返回页面或子类改 selection 后突然滚很远。
        for (int k = base - virtual_count * 3; k <= base + virtual_count * 3; ++k)
        {
            if (positiveMod(k, real_count) != real_index)
                continue;
            float d = fabsf((float)k - reference);
            if (d < best_dist)
            {
                best_dist = d;
                best = k;
            }
        }
        return best;
    }

    void syncMenuPositionToSelection()
    {
        int real_count = getMenuCount();
        if (real_count <= 0)
        {
            current_selection = 0;
            visual_selection = 0.0f;
            menu_target_position = 0;
            return;
        }

        int virtual_count = virtualMenuCount(real_count);
        current_selection = positiveMod(current_selection, real_count);
        menu_target_position = nearestVirtualPositionForRealIndex(visual_selection, current_selection, real_count, virtual_count);
        visual_selection = (float)menu_target_position;
    }

    void ensureMenuRuntimeState(int real_count)
    {
        if (real_count <= 0)
            return;

        int virtual_count = virtualMenuCount(real_count);
        current_selection = positiveMod(current_selection, real_count);

        int target_real = positiveMod(menu_target_position, real_count);
        if (target_real != current_selection)
        {
            menu_target_position = nearestVirtualPositionForRealIndex(visual_selection, current_selection, real_count, virtual_count);
            return;
        }

        // 子类有时会直接写 current_selection/visual_selection 后调用 drawMenuUI()。
        // 如果 visual_selection 已经被外部重置到当前真实索引附近，而 target 还停在旧循环圈，主动收敛，避免下一帧又滚回旧位置。
        if (fabsf(visual_selection - (float)menu_target_position) > (float)(virtual_count * 2) &&
            fabsf(visual_selection - (float)current_selection) < 0.35f)
        {
            menu_target_position = nearestVirtualPositionForRealIndex(visual_selection, current_selection, real_count, virtual_count);
        }
    }

    static String nextUtf8Unit(const char *text, int &pos)
    {
        if (!text || text[pos] == '\0')
            return String("");

        uint8_t c = (uint8_t)text[pos];
        int len = 1;
        if ((c & 0x80) == 0x00)
            len = 1;
        else if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;

        String out;
        for (int i = 0; i < len && text[pos + i] != '\0'; ++i)
            out += text[pos + i];
        pos += len;
        return out;
    }

    static void removeLastUtf8Unit(String &s)
    {
        int len = s.length();
        if (len <= 0)
            return;

        int cut = len - 1;
        while (cut > 0 && (((uint8_t)s[cut] & 0xC0) == 0x80))
            --cut;
        s.remove(cut);
    }

    static int clampUtf8Cut(const String &s, int cut)
    {
        int len = s.length();
        if (cut <= 0)
            return 0;
        if (cut >= len)
            return len;

        // cut 不能落在 UTF-8 续字节中间，否则 substring 后会产生乱码。
        while (cut > 0 && (((uint8_t)s[cut] & 0xC0) == 0x80))
            --cut;
        return cut;
    }

    String fitLineWithEllipsis(const String &src, int max_width) const
    {
        const char *ellipsis = "...";
        if (max_width <= 0)
            return String(ellipsis);

        if (HAL_Get_Text_Width_Font(src.c_str(), HAL_FONT_BODY) <= max_width)
            return src;

        if (HAL_Get_Text_Width_Font(ellipsis, HAL_FONT_BODY) >= max_width)
            return String(ellipsis);

        // 二分裁剪比逐字 remove + 反复测整行更轻，长选项页面快速滚动时不会被文本测宽拖慢。
        int lo = 0;
        int hi = src.length();
        String best = "";
        while (lo <= hi)
        {
            int raw_mid = (lo + hi) / 2;
            int mid = clampUtf8Cut(src, raw_mid);
            String candidate = src.substring(0, mid) + ellipsis;
            int w = HAL_Get_Text_Width_Font(candidate.c_str(), HAL_FONT_BODY);
            if (w <= max_width)
            {
                best = candidate;
                lo = raw_mid + 1;
            }
            else
            {
                hi = raw_mid - 1;
            }
        }
        return (best.length() > 0) ? best : String(ellipsis);
    }

    void clearMenuLayoutCache()
    {
        MenuLayoutCacheEntry *cache = sharedMenuLayoutCache();
        for (int i = 0; i < MENU_LAYOUT_CACHE_MAX; ++i)
        {
            cache[i].valid = false;
            cache[i].compact_valid = false;
            cache[i].wrapped_valid = false;
            cache[i].max_width = 0;
            cache[i].text = "";
        }
        sharedMenuLayoutCacheRealCount() = -1;
        sharedMenuLayoutCacheTextMaxWidth() = -1;
    }

    void prepareMenuLayoutCache(int real_count, int text_max_width)
    {
        if (sharedMenuLayoutCacheRealCount() == real_count &&
            sharedMenuLayoutCacheTextMaxWidth() == text_max_width)
            return;

        clearMenuLayoutCache();
        sharedMenuLayoutCacheRealCount() = real_count;
        sharedMenuLayoutCacheTextMaxWidth() = text_max_width;
    }

    MenuLayoutCacheEntry *findMenuLayoutCacheEntry(int index, const char *text, int max_width)
    {
        if (index < 0 || index >= MENU_LAYOUT_CACHE_MAX)
            return nullptr;

        const char *safe_text = text ? text : "";
        MenuLayoutCacheEntry &entry = sharedMenuLayoutCache()[index];
        if (!entry.valid || entry.max_width != max_width || !entry.text.equals(safe_text))
        {
            entry.valid = true;
            entry.compact_valid = false;
            entry.wrapped_valid = false;
            entry.max_width = max_width;
            entry.text = safe_text;
            entry.compact = WrappedMenuText();
            entry.wrapped = WrappedMenuText();
        }
        return &entry;
    }

    void buildCompactMenuText(const char *text, int max_width, WrappedMenuText &out) const
    {
        out = WrappedMenuText();
        if (!text || max_width <= 0)
            return;

        String one_line(text);
        int w = HAL_Get_Text_Width_Font(one_line.c_str(), HAL_FONT_BODY);
        if (w > max_width)
        {
            one_line = fitLineWithEllipsis(one_line, max_width);
            w = HAL_Get_Text_Width_Font(one_line.c_str(), HAL_FONT_BODY);
        }

        out.lines[0] = one_line;
        out.line_widths[0] = w;
        out.line_count = 1;
        out.width = w;
        out.height = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
    }

    void buildWrappedMenuText(const char *text, int max_width, WrappedMenuText &out) const
    {
        out = WrappedMenuText();
        if (!text || max_width <= 0)
            return;

        // 绝大多数菜单项一行就能放下，先走快路径，避免每个字符都测一次宽度。
        String full(text);
        int full_w = HAL_Get_Text_Width_Font(full.c_str(), HAL_FONT_BODY);
        if (full_w <= max_width)
        {
            out.lines[0] = full;
            out.line_widths[0] = full_w;
            out.line_count = 1;
            out.width = full_w;
            out.height = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
            return;
        }

        String current;
        int last_space = -1;
        int pos = 0;
        const uint8_t max_lines = 2;

        auto commitLine = [&](String line) {
            line.trim();
            if (line.length() == 0 || out.line_count >= max_lines)
                return;
            out.lines[out.line_count++] = line;
        };

        while (text[pos] != '\0')
        {
            String unit = nextUtf8Unit(text, pos);
            if (unit.length() == 0)
                break;

            String candidate = current + unit;
            int w = HAL_Get_Text_Width_Font(candidate.c_str(), HAL_FONT_BODY);
            if (w <= max_width || current.length() == 0)
            {
                current = candidate;
                if (unit == " ")
                    last_space = current.length() - 1;
                continue;
            }

            if (out.line_count == max_lines - 1)
            {
                // 最后一行溢出时不再开第三行，保留尽可能多的字符并追加省略号。
                current = fitLineWithEllipsis(candidate, max_width);
                break;
            }

            if (last_space > 0)
            {
                String head = current.substring(0, last_space);
                String tail = current.substring(last_space + 1) + unit;
                commitLine(head);
                current = tail;
            }
            else
            {
                commitLine(current);
                current = unit;
            }
            last_space = current.lastIndexOf(' ');
        }

        if (current.length() > 0 && out.line_count < max_lines)
            commitLine(current);

        int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        for (uint8_t i = 0; i < out.line_count; ++i)
        {
            int line_w = HAL_Get_Text_Width_Font(out.lines[i].c_str(), HAL_FONT_BODY);
            out.line_widths[i] = line_w;
            if (line_w > out.width)
                out.width = line_w;
        }
        out.height = (out.line_count == 0) ? line_h : (out.line_count * line_h + (out.line_count - 1) * 2);
    }

    const WrappedMenuText *getCachedCompactMenuText(int index, const char *text, int max_width, WrappedMenuText &fallback)
    {
        MenuLayoutCacheEntry *entry = findMenuLayoutCacheEntry(index, text, max_width);
        if (!entry)
        {
            buildCompactMenuText(text, max_width, fallback);
            return &fallback;
        }

        if (!entry->compact_valid)
        {
            buildCompactMenuText(entry->text.c_str(), max_width, entry->compact);
            entry->compact_valid = true;
        }
        return &entry->compact;
    }

    const WrappedMenuText *getCachedWrappedMenuText(int index, const char *text, int max_width, WrappedMenuText &fallback)
    {
        MenuLayoutCacheEntry *entry = findMenuLayoutCacheEntry(index, text, max_width);
        if (!entry)
        {
            buildWrappedMenuText(text, max_width, fallback);
            return &fallback;
        }

        if (!entry->wrapped_valid)
        {
            buildWrappedMenuText(entry->text.c_str(), max_width, entry->wrapped);
            entry->wrapped_valid = true;
        }
        return &entry->wrapped;
    }

    bool updateCurveEnergy()
    {
        uint32_t now = millis();
        if (menu_curve_last_tick == 0)
        {
            menu_curve_last_tick = now;
            return false;
        }

        uint32_t elapsed = now - menu_curve_last_tick;
        menu_curve_last_tick = now;
        if (elapsed == 0 || menu_curve_energy <= 0.0f)
            return false;

        float old = menu_curve_energy;
        // 约 300~400ms 衰减回静态曲率，快速转动结束后视觉会自然收回。
        menu_curve_energy -= (float)elapsed * 0.0028f;
        if (menu_curve_energy < 0.0f)
            menu_curve_energy = 0.0f;
        return fabsf(old - menu_curve_energy) > 0.01f;
    }

    float targetMenuSlingStrength(float v_pos) const
    {
        // 甩动的目标值仍然只由“正在追赶目标”触发：菜单停稳后目标为 0，
        // 但真正绘制值会通过 updateMenuSlingDisplay() 平滑回落，不再一帧内硬切。
        float pending = fabsf((float)menu_target_position - v_pos);
        if (pending <= 0.015f || menu_curve_energy <= 0.01f)
            return 0.0f;

        float motion_gate = pending / 0.75f;
        if (motion_gate > 1.0f)
            motion_gate = 1.0f;

        // smoothstep 让刚开始滚动和即将停下时都更柔和，避免速度效果突然出现/消失。
        motion_gate = motion_gate * motion_gate * (3.0f - 2.0f * motion_gate);
        return menu_curve_energy * motion_gate;
    }

    bool updateMenuSlingDisplay()
    {
        uint32_t now = millis();
        if (menu_sling_last_tick == 0)
        {
            menu_sling_last_tick = now;
            return false;
        }

        uint32_t elapsed = now - menu_sling_last_tick;
        menu_sling_last_tick = now;
        if (elapsed == 0)
            return false;

        if (elapsed > 48)
            elapsed = 48;

        float target = targetMenuSlingStrength(visual_selection);
        float old = menu_sling_display;

        // 上升稍快，回收稍慢：快速旋转时能马上甩起来，停下时柔和回到中心。
        float rate = (target > menu_sling_display) ? 0.018f : 0.010f;
        float alpha = 1.0f - expf(-(float)elapsed * rate);
        menu_sling_display += (target - menu_sling_display) * alpha;

        if (menu_sling_display < 0.004f && target <= 0.001f)
            menu_sling_display = 0.0f;
        if (menu_sling_display > 1.0f)
            menu_sling_display = 1.0f;

        return fabsf(old - menu_sling_display) > 0.002f;
    }

    float menuVerticalOffset(float offset, int spacing_y, float sling_strength) const
    {
        float distance = fabsf(offset);
        if (distance <= 0.001f)
            return 0.0f;

        float sign = (offset >= 0.0f) ? 1.0f : -1.0f;

        // 静止时保持稳定、可读的间距；只有滚动中的速度甩动会轻微拉开第一圈条目。
        // 第二圈之后仍然压缩间距，避免最外侧条目和上一项突然离得很远。
        float first_gap = 1.14f + sling_strength * 0.08f;
        float outer_gap = 0.74f + sling_strength * 0.06f;
        float units = 0.0f;
        if (distance <= 1.0f)
            units = distance * first_gap;
        else
            units = first_gap + (distance - 1.0f) * outer_gap;

        return sign * (float)spacing_y * units;
    }

    float menuCurveOffsetX(float offset, float curve_factor) const
    {
        float distance = fabsf(offset);
        if (distance <= 0.001f)
            return 0.0f;

        // 水平曲率不再直接用 offset² 无限放大，避免第三圈条目过早顶到屏幕右边被夹死。
        // 轻微幂曲线能保留 3D 右退感，同时让快速旋转时的曲率变化更可见。
        float d = min(distance, 2.7f);
        return powf(d, 1.62f) * curve_factor;
    }

    float menuSlingOffsetX(float offset, float sling_strength) const
    {
        float distance = fabsf(offset);
        if (distance <= 0.05f || sling_strength <= 0.01f)
            return 0.0f;

        // 速度甩动方向向左；停止后由 menu_sling_display 平滑回收到 0，
        // 中心项本身由 center_x 参与甩动，外圈条目在这里叠加更轻的惯性偏移。
        float d = min(distance, 3.0f);
        float speed_curve = powf(d, 1.55f) * UITheme::Menu::CurveSpeedBoost();
        float speed_sling = powf(d, 1.12f) * UITheme::Menu::SlingSpeedBoost();
        return -(speed_curve + speed_sling) * sling_strength;
    }

    void drawWrappedMenuText(int x, int y, const WrappedMenuText &layout, float distance, uint16_t color)
    {
        int line_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        for (uint8_t i = 0; i < layout.line_count; ++i)
        {
            int line_w = layout.line_widths[i];
            if (line_w <= 0)
                line_w = HAL_Get_Text_Width_Font(layout.lines[i].c_str(), HAL_FONT_BODY);
            int line_x = x + (layout.width - line_w) / 2;
            int line_y = y + i * (line_h + 2);
            HAL_Screen_ShowChineseLine_Faded_Color(line_x, line_y, layout.lines[i].c_str(), distance, color);
        }
    }

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

    // 【接口说明】子类可选择菜单视觉样式；输入、条目内容和点击行为不随样式变化。
    virtual UIMenuRenderStyle getMenuRenderStyle() const
    {
        return UI_GetMenuRenderStyle();
    }

    // 【兼容实现】当前滚轮渲染器使用的原有整屏绘制逻辑。
    // 后续样式通过 UIMenuRenderer 接口接管绘制，不应再从页面直接调用此函数。
    void drawWheelMenuUI(float v_pos)
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

        WrappedMenuText selected_fallback;
        const WrappedMenuText *selected_layout = getCachedWrappedMenuText(display_selection,
                                                                          getItemText(display_selection),
                                                                          text_max_w,
                                                                          selected_fallback);

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

        // 静止布局：选中项回到 HUD 竖线右侧区域的视觉中心，保证停下来时页面是稳定居中的。
        // 动态布局：只有滚轮仍在追赶目标位置时，中心项才随速度向左产生弹性甩动；
        // 这样中心项不会一直固定在原地，也不会在停稳后残留偏移。
        int selected_text_w_for_pos = max(12, (int)lroundf(menu_label_width));
        int rest_center_x = right_panel_x + right_panel_w / 2;
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
        // 外层暗框先画一圈，选中框在高速滚动和复杂背景下仍能保持清晰边界。
        HAL_Draw_Rect(box_x - 2, box_y - 2, box_w + 4, dynamic_box_h + 4, UITheme::COLOR_DIM);
        HAL_Draw_Rect(box_x, box_y, box_w, dynamic_box_h, 1);
        const int bracket = min(9, max(5, dynamic_box_h / 3));
        HAL_Draw_Line(box_x - 3, box_y, box_x + bracket, box_y, 1);
        HAL_Draw_Line(box_x - 3, box_y, box_x - 3, box_y + bracket, 1);
        HAL_Draw_Line(box_x + box_w + 2, box_y, box_x + box_w - bracket, box_y, 1);
        HAL_Draw_Line(box_x + box_w + 2, box_y, box_x + box_w + 2, box_y + bracket, 1);
        HAL_Draw_Line(box_x - 3, box_y + dynamic_box_h, box_x + bracket, box_y + dynamic_box_h, 1);
        HAL_Draw_Line(box_x - 3, box_y + dynamic_box_h, box_x - 3, box_y + dynamic_box_h - bracket, 1);
        HAL_Draw_Line(box_x + box_w + 2, box_y + dynamic_box_h, box_x + box_w - bracket, box_y + dynamic_box_h, 1);
        HAL_Draw_Line(box_x + box_w + 2, box_y + dynamic_box_h, box_x + box_w + 2, box_y + dynamic_box_h - bracket, 1);

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
            const char *text = getItemText(real_idx);

            WrappedMenuText item_fallback;
            const WrappedMenuText *item_layout = nullptr;
            bool is_center_slot = distance < 0.55f;
            if (is_center_slot && real_idx == display_selection)
                item_layout = selected_layout;
            else
                item_layout = getCachedWrappedMenuText(real_idx, text, text_max_w, item_fallback);

            // 展开权重随槽位距离连续变化，而不是在中心槽门限处突然切换两行。
            float focus = max(0.0f, 1.0f - distance);
            focus = focus * focus * (3.0f - 2.0f * focus);
            const float expansion = menu_expansion * focus;
            int text_width = item_layout->width;
            int base_x = center_x - (text_width / 2);

            // 3D 侧弯仍保持右退，速度甩动则向左；二者叠加后能形成更明显的旋钮惯性。
            int item_x = base_x + (int)(menuCurveOffsetX(offset, UI_3D_CURVE_FACTOR) + menuSlingOffsetX(offset, active_sling));

            if (item_x + text_width > sw - 5)
                item_x = sw - text_width - 5;
            if (item_x < right_panel_x + 2)
                item_x = right_panel_x + 2;

            const char *p_pref = nullptr, *p_val = nullptr, *p_suff = nullptr;

            if (is_center_slot && real_idx == display_selection && getItemEditParts(real_idx, &p_pref, &p_val, &p_suff))
            {
                int edit_w = HAL_Get_Text_Width(p_pref ? p_pref : "") +
                             HAL_Get_Text_Width(p_val ? p_val : "") +
                             HAL_Get_Text_Width(p_suff ? p_suff : "");
                int edit_x = center_x - edit_w / 2 + (int)(menuCurveOffsetX(offset, UI_3D_CURVE_FACTOR) + menuSlingOffsetX(offset, active_sling));
                if (edit_x + edit_w > sw - 5)
                    edit_x = sw - edit_w - 5;
                if (edit_x < right_panel_x + 2)
                    edit_x = right_panel_x + 2;
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

    // 【接口说明】页面只请求“按当前样式重绘”，不依赖具体的菜单布局。
    void drawMenuUI(float v_pos)
    {
        ensureMenuRuntimeState(getMenuCount());
        // 兼容现有页面直接指定 visual_selection 后重绘；外部定位不继承旧速度。
        if (fabsf(v_pos - menu_motion_position) > 0.001f)
        {
            menu_velocity = 0.0f;
            menu_motion_position = v_pos;
            menu_settled = false;
        }
        menu_frame_gate.reset();
        // 旧页面仍可直接定位 visual_selection；通知纸带丢弃旧惯性，避免下一帧滚回原处。
        if (getMenuRenderStyle() == UIMenuRenderStyle::PaperTape)
        {
            auto &renderer = UI_MenuRendererForStyle(getMenuRenderStyle());
            if (fabsf(renderer.position() - v_pos) > 0.001f)
                renderer.reset(v_pos);
        }
        UI_MenuRendererForStyle(getMenuRenderStyle()).draw(*this, v_pos);
    }

public:
    // 【渲染适配接口】给独立菜单渲染器读取当前页面内容；不改变原有子类虚函数和业务行为。
    int getMenuRenderCount() { return getMenuCount(); }
    const char *getMenuRenderTitle() { return getTitle(); }
    const char *getMenuRenderItemText(int index) { return getItemText(index); }
    bool getMenuRenderItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix)
    {
        return getItemEditParts(index, prefix, anim_val, suffix);
    }
    uint16_t getMenuRenderItemColor(int index) { return getItemColor(index); }
    int getMenuRenderSelection() const { return current_selection; }
    float getMenuRenderSlingDisplay() const { return menu_sling_display; }

    // 【文本契约】复用菜单两行 UTF-8 排版与共享缓存，借用字符串当帧消费，不保留页面静态缓冲指针。
    void drawMenuRenderLabel(int index, int center_x, int center_y, int max_width, uint16_t color)
    {
        WrappedMenuText fallback;
        const WrappedMenuText *layout = getCachedWrappedMenuText(index, getItemText(index), max_width, fallback);
        drawWrappedMenuText(center_x - layout->width / 2, center_y - layout->height / 2, *layout, 0.0f, color);
    }

    // 【渲染适配接口】编辑态菜单仍复用 AppBase 的统一数值动画，渲染器不复制动画算法。
    void drawMenuRenderEditText(int x, int y,
                                const char *prefix,
                                const char *anim_val,
                                const char *suffix,
                                float distance = 0.0f)
    {
        drawSegmentedAnimatedText(x, y, prefix, anim_val, suffix, distance);
    }

    // 【函数说明】菜单首次进入时把选中项和视觉位置归零，并立刻绘制菜单。
    void onCreate() override
    {
        current_selection = 0;
        visual_selection = 0.0f;
        menu_target_position = 0;
        menu_curve_energy = 0.0f;
        menu_sling_display = 0.0f;
        menu_last_knob_tick = 0;
        menu_curve_last_tick = millis();
        menu_sling_last_tick = millis();
        clearMenuLayoutCache();
        onResume();
    }

    // 【接口说明】从子页面返回时按当前 visual_selection 重绘菜单。
    void onResume() override
    {
        syncMenuPositionToSelection();
        menu_velocity = 0.0f;
        menu_motion_position = visual_selection;
        menu_anim_last_tick = millis();
        menu_frame_dt = 0.0f;
        menu_redraw_pending = false;
        menu_layout_animating = false;
        menu_settled = false;
        menu_expansion = 0.0f;
        menu_box_height = 0.0f;
        menu_label_width = 0.0f;
        menu_sling_display = 0.0f;
        menu_sling_last_tick = millis();
        clearMenuLayoutCache();
        UI_MenuRendererForStyle(getMenuRenderStyle()).reset(visual_selection);
        drawMenuUI(visual_selection);
    }
    void onBackground() override {}

    // 【函数说明】推进菜单滑动插值、编辑值跳动动画，并在 HUD/电量变化时重绘页面。
    void onLoop() override
    {
        // HUD/电量变化先锁存，下一绘制帧统一消费，避免帧门控丢失一次性变化通知。
        menu_redraw_pending |= UIHud_NeedsRedraw();
        menu_redraw_pending |= sysPower.needsRedraw();
        if (!menu_frame_gate.due(UITheme::FRAME_FAST_MS))
            return;
        const uint32_t frame_now = millis();
        const uint32_t elapsed = frame_now - menu_anim_last_tick;
        menu_anim_last_tick = frame_now;
        // 间隔上限只过滤长期离开页面/调试暂停；正常掉帧按真实时间推进。
        menu_frame_dt = min(elapsed, (uint32_t)100) * 0.001f;
        bool needs_redraw = updateEditAnimation();
        needs_redraw |= menu_redraw_pending || menu_layout_animating;
        menu_redraw_pending = false;
        if (updateCurveEnergy())
            needs_redraw = true;
        if (updateMenuSlingDisplay())
            needs_redraw = true;

        int real_count = getMenuCount();
        if (real_count == 0)
            return;
        ensureMenuRuntimeState(real_count);

        float target = (float)menu_target_position;

        if (getMenuRenderStyle() == UIMenuRenderStyle::PaperTape)
        {
            auto &renderer = UI_MenuRendererForStyle(getMenuRenderStyle());
            if (renderer.update(target, millis()))
            {
                visual_selection = renderer.position();
                needs_redraw = true;
            }
        }
        else
        {
            needs_redraw |= UI_MenuAdvanceDamped(target, menu_frame_dt, UITheme::Menu::MotionResponse,
                                                 visual_selection, menu_velocity);
            menu_motion_position = visual_selection;
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
            int count = virtualMenuCount(real_count);
            ensureMenuRuntimeState(real_count);

            menu_target_position += delta;
            current_selection = positiveMod(menu_target_position, real_count);

            uint32_t now = millis();
            uint32_t gap = (menu_last_knob_tick == 0) ? 80 : (now - menu_last_knob_tick);
            menu_last_knob_tick = now;
            if (gap < 8)
                gap = 8;

            // delta 越大、两次旋转间隔越短，曲率能量越高；能量在 onLoop 中自然衰减。
            float speed_factor = 80.0f / (float)gap;
            float impulse = min(0.55f, 0.12f * fabsf((float)delta) + 0.10f * speed_factor);
            menu_curve_energy += impulse;
            if (menu_curve_energy > 1.0f)
                menu_curve_energy = 1.0f;

            // 防止 target 长时间累计到非常大的数值。只在视觉位置也已经接近时归一化，不影响快速滚动动画。
            if (abs(menu_target_position) > count * 64 && fabsf(visual_selection - (float)menu_target_position) < 0.5f)
            {
                int normalized = positiveMod(menu_target_position, count);
                const int shift = menu_target_position - normalized;
                menu_target_position = normalized;
                // 只平移连续坐标，保留尚未收敛的位移和速度，跨循环归一化时不跳帧。
                visual_selection -= shift;
                menu_motion_position -= shift;
            }

            SYS_SOUND_GLITCH();
        }
    }

    // 【函数说明】短按确认当前真实条目，先播放确认反馈再调用子类 onItemClicked。
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        int real_count = getMenuCount();
        if (real_count > 0)
        {
            current_selection = positiveMod(current_selection, real_count);
            onItemClicked(current_selection);
        }
    }

    // 【接口说明】长按交给子类 onLongPressed，实现返回、保存退出等页面规则。
    void onKeyLong() override { onLongPressed(); }
};

#endif
