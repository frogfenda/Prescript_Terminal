// 文件：src/apps/app_gacha.cpp
#include "app_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys_haptic.h"
#include "sys/sys_res.h"
#include "sys_config.h" // 【新增】：引入全能系统管家

struct PullResult
{
    IdentityData *id_ptr;
    bool is_new;
};

class AppGacha : public AppBase
{
private:
    PullResult current_pulls[10];
    int phase = 0;
    uint32_t anim_timer = 0;
    uint32_t last_draw_time = 0;

    int max_star_pulled = 1;
    bool has_walp_pulled = false;

    // 动画引擎：左到右线性揭晓。
    // 10 个身份方块按一行排布，扫描线从左到右扫过，扫到方块中心后揭示稀有度颜色。
    static constexpr int PULL_COUNT = 10;
    bool revealed[PULL_COUNT];
    const uint32_t ANIM_DURATION = 1800; // 扫描线扫过全部方块的总时长。

    // 结果列表滚动偏移。可见行数不再写死，drawResultPhase/onKnob 会按当前屏幕高度动态计算。
    int m_scroll_offset = 0;

    struct ScanLayout
    {
        int box_size;
        int gap_x;
        int total_w;
        int start_x;
        int start_y;
        int title_y;
        int scan_pad;
    };

    /**
     * 计算十连扫描动画的响应式布局。
     *
     * 旧版本固定使用 18px 方块和 6px 间距，刚好适配 284×76；
     * 新屏 428×142 下如果继续使用旧参数，扫描阵列会显得过小且留白过多。
     * 这里按屏幕宽度重新计算方块大小，让 10 个结果方块仍然一行排满，
     * 同时限制最大尺寸，避免方块过大后压缩标题和扫描线空间。
     */
    ScanLayout calcScanLayout(int sw, int sh)
    {
        ScanLayout l;
        int side_margin = max(24, sw / 14);
        l.gap_x = max(7, sw / 54);
        l.box_size = (sw - side_margin * 2 - l.gap_x * (PULL_COUNT - 1)) / PULL_COUNT;
        l.box_size = constrain(l.box_size, 20, 30);
        l.total_w = PULL_COUNT * l.box_size + (PULL_COUNT - 1) * l.gap_x;
        l.start_x = (sw - l.total_w) / 2;
        l.title_y = max(10, sh / 10);
        l.start_y = (sh * 58) / 100 - l.box_size / 2;
        if (l.start_y < l.title_y + HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 8)
            l.start_y = l.title_y + HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 8;
        if (l.start_y + l.box_size > sh - 10)
            l.start_y = sh - l.box_size - 10;
        l.scan_pad = max(5, l.box_size / 4);
        return l;
    }

    /**
     * 根据当前字体和屏幕高度计算结果列表可见行数。
     * 每行显示“星级 + 人格名”，行高需要跟随字体放大；右侧滚动条也使用同一行数计算。
     */
    int getResultRowHeight() const
    {
        return max(22, HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 2);
    }

    int getResultVisibleRows(int sh) const
    {
        int row_h = getResultRowHeight();
        int visible = (sh - 14) / row_h;
        return constrain(visible, 3, PULL_COUNT);
    }

    // ==========================================
    // 底层引擎：抽卡概率与数据分配
    // ==========================================
    IdentityData *rollSingle(bool is_guaranteed_2star)
    {
        if (g_gacha_pool_total == 0)
            return nullptr;

        int r = random(1000);
        int target_star = 1;
        if (r < 29)
            target_star = 3;
        else if (r < 29 + 128)
            target_star = 2;
        else if (is_guaranteed_2star)
            target_star = 2;

        if (target_star == 3 && g_count_3star == 0)
            target_star = 2;
        if (target_star == 2 && g_count_2star == 0)
            target_star = 1;
        if (target_star == 1 && g_count_1star == 0)
            return &g_gacha_pool[0];

        int selected_idx = 0;
        if (target_star == 3)
            selected_idx = g_gacha_3star[random(g_count_3star)];
        else if (target_star == 2)
            selected_idx = g_gacha_2star[random(g_count_2star)];
        else
            selected_idx = g_gacha_1star[random(g_count_1star)];

        return &g_gacha_pool[selected_idx];
    }

    void executeTenPull()
    {
        max_star_pulled = 1;
        has_walp_pulled = false;

        for (int i = 0; i < PULL_COUNT; i++)
        {
            current_pulls[i].id_ptr = rollSingle(i == 9);
            current_pulls[i].is_new = false;
            if (current_pulls[i].id_ptr)
            {
                int s = current_pulls[i].id_ptr->star;
                int w = current_pulls[i].id_ptr->walp;

                if (s > max_star_pulled) max_star_pulled = s;
                if (w == 1) has_walp_pulled = true;

                // ==========================================
                // 【新增】：核心统计逻辑，直接存入系统管家的内存树
                // ==========================================
                sysConfig.gacha_stats.total++;
                if (s == 3) sysConfig.gacha_stats.s3++;
                else if (s == 2) sysConfig.gacha_stats.s2++;
                else if (s == 1) sysConfig.gacha_stats.s1++;

                if (w == 1) {
                    if (s == 3) sysConfig.gacha_stats.w3++;
                    else if (s == 2) sysConfig.gacha_stats.w2++;
                }
            }
            revealed[i] = false; 
        }
        
        sysConfig.save(); // 【新增】：十连结束瞬间，将公共配置和当前语言配置永久覆写至硬盘！
    }

    void skipToResult()
    {
        phase = 2;
        m_scroll_offset = 0;
        // 最终结果汇总音效
        if (has_walp_pulled || max_star_pulled == 3)
        {
            sysAudio.playTone(2000, 300);
            sysHaptic.playConfirm();
        }
        else
        {
            sysAudio.playTone(1000, 100);
        }
        drawUI();
    }

    // ==========================================
    // UI 渲染：待机界面
    // ==========================================
    void drawIdlePhase(int sw, int sh)
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        const char* title = "MEPHISTOPHELES";
        const char* hint = zh ? "[ 单击 ] 执行十连提取" : "[ CLICK ] 10X EXTRACT";

        // 待机页只显示标题和操作提示；使用当前字体宽度居中，避免换字体后仍按旧像素偏移。
        int title_x = (sw - HAL_Get_Text_Width(title)) / 2;
        int hint_x = (sw - HAL_Get_Text_Width(hint)) / 2;
        int title_y = sh / 2 - HAL_Get_Font_Line_Height(HAL_FONT_BODY) - 8;
        int hint_y = sh / 2 + 8;

        HAL_Screen_ShowChineseLine_Faded_Color(title_x, title_y, title, 0.0f, TFT_RED);
        HAL_Screen_ShowChineseLine_Faded_Color(hint_x, hint_y, hint, 0.0f, TFT_WHITE);
    }

    // ==========================================
    // UI 渲染：【线性阵列解密】硬核扫描动画
    // ==========================================
    void drawAnimPhase(int sw, int sh)
    {
        uint32_t elapsed = millis() - anim_timer;
        ScanLayout layout = calcScanLayout(sw, sh);

        // 顶部状态文字表示提取设备正在扫描身份矩阵。
        // 文字居中绘制，颜色压暗，避免抢走十连扫描方块的视觉焦点。
        const char* label = "EXTRACTION...";
        int label_x = (sw - HAL_Get_Text_Width(label)) / 2;
        HAL_Screen_ShowChineseLine_Faded_Color(label_x, layout.title_y, label, 0.0f, 0x18E3);

        // 绘制 10 个身份方块。未揭示时是暗色空框；扫描线扫过后填充稀有度颜色。
        for (int i = 0; i < PULL_COUNT; i++)
        {
            int x = layout.start_x + i * (layout.box_size + layout.gap_x);
            int y = layout.start_y;

            if (revealed[i])
            {
                IdentityData *p_id = current_pulls[i].id_ptr;
                uint16_t box_color = TFT_DARKGREY;
                if (p_id)
                {
                    if (p_id->walp == 1)
                        box_color = TFT_GREEN;
                    else if (p_id->star == 3)
                        box_color = TFT_GOLD;
                    else if (p_id->star == 2)
                        box_color = TFT_RED;
                }

                HAL_Fill_Rect(x, y, layout.box_size, layout.box_size, box_color);
                HAL_Draw_Rect(x, y, layout.box_size, layout.box_size, TFT_WHITE);
            }
            else
            {
                HAL_Draw_Rect(x, y, layout.box_size, layout.box_size, 0x39E7);
            }
        }

        // 绘制扫描线。扫描线高度随方块尺寸扩展，形成穿过整排方块的“提取扫描”效果。
        if (elapsed < ANIM_DURATION)
        {
            int scan_x = layout.start_x + (elapsed * layout.total_w / ANIM_DURATION);
            HAL_Draw_Line(scan_x, layout.start_y - layout.scan_pad, scan_x, layout.start_y + layout.box_size + layout.scan_pad, TFT_CYAN);
            HAL_Draw_Line(scan_x - 1, layout.start_y - layout.scan_pad / 2, scan_x - 1, layout.start_y + layout.box_size + layout.scan_pad / 2, 0x03E0);
        }
    }

    // ==========================================
    // UI 渲染：提取结算列表
    // ==========================================
    void drawResultPhase(int sw, int sh)
    {
        int row_h = getResultRowHeight();
        int visible_rows = getResultVisibleRows(sh);
        int top_y = max(6, (sh - visible_rows * row_h) / 2);

        for (int i = 0; i < visible_rows; i++)
        {
            int list_idx = m_scroll_offset + i;
            if (list_idx >= PULL_COUNT)
                break;

            IdentityData *p_id = current_pulls[list_idx].id_ptr;
            if (!p_id)
                continue;

            uint16_t theme_color = TFT_DARKGREY;
            String star_str = "★";
            if (p_id->star == 2)
            {
                theme_color = TFT_RED;
                star_str = "★★";
            }
            if (p_id->star == 3)
            {
                theme_color = TFT_GOLD;
                star_str = "★★★";
            }
            if (p_id->walp == 1)
            {
                theme_color = TFT_GREEN;
            }

            String w_str = (p_id->walp == 1) ? "[W] " : "";
            String line_text = w_str + star_str + " " + p_id->sinner + " : " + p_id->id_name;

            int text_w = HAL_Get_Text_Width(line_text.c_str());
            int x_pos = (sw - 14 - text_w) / 2;
            if (x_pos < 8)
                x_pos = 8;

            int y_pos = top_y + i * row_h;
            HAL_Screen_ShowChineseLine_Faded_Color(x_pos, y_pos, line_text.c_str(), 0.0f, theme_color);
        }

        // 右侧滚动条按新屏高度计算。旧版本 track_h 固定 64，在 142 高屏幕上会显得短。
        int max_offset = PULL_COUNT - visible_rows;
        if (max_offset > 0)
        {
            int track_top = 8;
            int track_h = sh - track_top * 2;
            int bar_h = track_h * visible_rows / PULL_COUNT;
            if (bar_h < 6)
                bar_h = 6;
            int bar_y = track_top + (track_h - bar_h) * m_scroll_offset / max_offset;

            HAL_Fill_Rect(sw - 8, bar_y, 3, bar_h, TFT_WHITE);
        }
    }

    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        if (phase == 0)
            drawIdlePhase(sw, sh);
        else if (phase == 1)
            drawAnimPhase(sw, sh);
        else if (phase == 2)
            drawResultPhase(sw, sh);

        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        phase = 0;
        m_scroll_offset = 0;
        drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override
    {
        if (phase == 1)
        {
            uint32_t now = millis();
            uint32_t elapsed = now - anim_timer;

            // 动画放完后，停顿 0.4 秒让用户看完 10 个方块的全貌，然后再切到结算列表
            if (elapsed > ANIM_DURATION + 40)
            {
                skipToResult();
            }
            else
            {
                // ==========================================
                // 【核心逻辑】：物理级精准判断扫描线与方块的交集
                // ==========================================
                ScanLayout layout = calcScanLayout(HAL_Get_Screen_Width(), HAL_Get_Screen_Height());

                for (int i = 0; i < PULL_COUNT; i++)
                {
                    // 计算扫描线扫到第 i 个方块中心时所需的时间。
                    // 方块尺寸和间距与 drawAnimPhase 使用同一套响应式布局，
                    // 保证视觉扫描线和音效/震动触发点严格一致。
                    int trigger_x = i * (layout.box_size + layout.gap_x) + layout.box_size / 2;
                    uint32_t trigger_time = trigger_x * ANIM_DURATION / layout.total_w;

                    // 当扫描线压过中心点，且这个方块还没被揭晓时
                    if (elapsed >= trigger_time && !revealed[i])
                    {
                        revealed[i] = true;
                        IdentityData *p_id = current_pulls[i].id_ptr;

                        // 【不同程度的音效震动反馈】
                        if (p_id)
                        {
                            if (p_id->walp == 1)
                            {
                                sysAudio.playTone(2500, 160); // 瓦夜：最长、最高亢的异响
                                sysHaptic.playTick();
                            }
                            else if (p_id->star == 3)
                            {
                                sysAudio.playTone(3200, 120); // 3星：清脆高音
                                sysHaptic.playTick();
                            }
                            else if (p_id->star == 2)
                            {
                                sysAudio.playTone(1500, 70); // 2星：标准中音
                            }
                            else
                            {
                                sysAudio.playTone(800, 30); // 1星：沉闷短促的“哒”
                            }
                        }
                    }
                }

                // 30FPS 动画帧率锁，保证扫描线移动丝滑且不撕裂
                if (now - last_draw_time > 33)
                {
                    last_draw_time = now;
                    drawUI();
                }
            }
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (phase == 2)
        {
            m_scroll_offset += delta;
            int max_offset = PULL_COUNT - getResultVisibleRows(HAL_Get_Screen_Height());
            if (max_offset < 0) max_offset = 0;

            if (m_scroll_offset < 0)
                m_scroll_offset = 0;
            if (m_scroll_offset > max_offset)
                m_scroll_offset = max_offset;

            SYS_SOUND_GLITCH();
            drawUI();
        }
    }

    void onKeyShort() override
    {
        if (phase == 0)
        {
            SYS_SOUND_CONFIRM();
            executeTenPull();
            phase = 1;
            anim_timer = millis();
            last_draw_time = millis();
        }
        else if (phase == 1)
        {
            // 一键跳过演出，直达结果！
            SYS_SOUND_CONFIRM();
            skipToResult();
        }
        else if (phase == 2)
        {
            SYS_SOUND_CONFIRM();
            phase = 0;
            drawUI();
        }
    }

    void onKeyLong() override
    {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppGacha instanceGacha;
AppBase *appGacha = &instanceGacha;
