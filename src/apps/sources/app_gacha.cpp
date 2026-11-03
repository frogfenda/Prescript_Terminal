// 文件：src/apps/app_gacha.cpp
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys/sys_haptic.h"
#include "sys/resources/sys_identity_catalog.h"
#include "sys/sys_config.h" // 【新增】：引入全能系统管家
#include "lang/ui_strings.h"
#include "ui/ui_gacha.h"
#include "ui/ui_clock.h"

struct PullResult
{
    // 每次抽取保存身份值副本，不持有目录内部指针；运行时切换语言后结算页仍然安全。
    IdentityData identity;
    bool valid = false;
    bool is_new = false;
};

class AppGacha : public AppBase
{
private:
    PullResult current_pulls[UIGacha::PULL_COUNT];
    int phase = 0;
    uint32_t anim_timer = 0;
    uint32_t idle_timer = 0;
    uint32_t last_draw_time = 0;

    int max_star_pulled = 1;
    bool has_walp_pulled = false;
    // 抽取时只更新内存，结果已上屏后再保存公共统计，避免 Flash 写入挡住按下后的首帧。
    bool stats_dirty = false;

    // 动画只负责表现一次完整的“提取过程”，身份结果在动画结束后统一交给原有结算页显示。
    static constexpr int PULL_COUNT = UIGacha::PULL_COUNT;
    UIGacha::ExtractionAnimation extraction_animation;

    // 结果列表滚动偏移。可见行数不再写死，drawResultPhase/onKnob 会按当前屏幕高度动态计算。
    int m_scroll_offset = 0;

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
    bool rollSingle(bool is_guaranteed_2star, IdentityData &out)
    {
        if (SysIdentityCatalog::Count() == 0)
            return false;

        int r = random(1000);
        int target_star = 1;
        if (r < 29)
            target_star = 3;
        else if (r < 29 + 128)
            target_star = 2;
        else if (is_guaranteed_2star)
            target_star = 2;

        if (target_star == 3 && !SysIdentityCatalog::HasStar(3))
            target_star = 2;
        if (target_star == 2 && !SysIdentityCatalog::HasStar(2))
            target_star = 1;
        if (SysIdentityCatalog::DrawByStar((uint8_t)target_star, out))
            return true;

        // 异常素材可能缺少一整档星级；按低到高寻找任意可用记录，避免应用崩溃。
        for (uint8_t star = 1; star <= 3; ++star)
        {
            if (SysIdentityCatalog::DrawByStar(star, out))
                return true;
        }
        return false;
    }

    void executeTenPull()
    {
        max_star_pulled = 1;
        has_walp_pulled = false;

        for (int i = 0; i < PULL_COUNT; i++)
        {
            current_pulls[i].valid = rollSingle(i == 9, current_pulls[i].identity);
            current_pulls[i].is_new = false;
            if (current_pulls[i].valid)
            {
                int s = current_pulls[i].identity.star;
                int w = current_pulls[i].identity.walp;

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
        }
        
        stats_dirty = true;
    }

    void savePendingStats()
    {
        if (!stats_dirty) return;
        // gacha_stats 属于公共配置；不额外重写与本次十连无关的语言配置。
        sysConfig.saveCommon();
        stats_dirty = false;
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
        savePendingStats(); // 正式结果先上屏；跳过与自然结束共用同一保存点。
    }

    // ==========================================
    // UI 渲染：待机界面
    // ==========================================
    void enterIdlePhase()
    {
        // 每次进入待抽页等概率选一种演出并保持到结算；重复抽取时可以再次抽到相同样式。
        // 此处只准备画面，不抽身份、不写统计；下一次短按才开始十连。
        UIGacha::PullAppearance neutral[PULL_COUNT] = {};
        const auto style = random(2) == 0 ? UIGacha::AnimationStyle::MirrorShards
                                          : UIGacha::AnimationStyle::BusWindow;
        extraction_animation.Start((uint32_t)random(1, 0x7FFFFFFF), neutral,
                                   HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), style);
        phase = 0;
        m_scroll_offset = 0;
        idle_timer = millis();
        last_draw_time = idle_timer;
        drawUI();
    }

    void drawIdlePhase()
    {
        extraction_animation.DrawIdle(millis() - idle_timer);
    }

    // ==========================================
    // UI 渲染：Mephistopheles 引擎提取演出
    // ==========================================
    void drawAnimPhase(int sw, int sh)
    {
        const uint32_t now = millis();
        const uint32_t elapsed = now - anim_timer;
        // 镜片侧翻时露出的是正式列表，不是另一份占位结果；列表数据/滚动/配色仍由本 App 拥有。
        if (elapsed >= extraction_animation.FlipStartMs())
            drawResultPhase(sw, sh);
        extraction_animation.Draw(elapsed, now - idle_timer);
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

            const IdentityData *p_id = current_pulls[list_idx].valid ? &current_pulls[list_idx].identity : nullptr;
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
            drawIdlePhase();
        else if (phase == 1)
            drawAnimPhase(sw, sh);
        else if (phase == 2)
            drawResultPhase(sw, sh);

        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        enterIdlePhase();
    }

    void onResume() override { drawUI(); }

    void onLoop() override
    {
        if (phase == 0)
        {
            // 待抽也按公共帧门控刷新：碎镜轻晃或巴士持续行驶，等待多久都不会自动抽取。
            if (UIClock_Due(last_draw_time, UITheme::FRAME_FAST_MS)) drawUI();
        }
        if (phase == 1)
        {
            uint32_t now = millis();
            uint32_t elapsed = now - anim_timer;

            // 保持 → 翻面已经包含在 UI 时间轴内；结束时直接完成状态与汇总反馈，不再补一段空等。
            if (elapsed >= extraction_animation.DurationMs())
            {
                skipToResult();
            }
            else
            {
                // 复用公共帧门控；用绝对经过时间采样，掉帧时不拉长演出，也不阻塞按键/系统服务。
                if (UIClock_Due(last_draw_time, UITheme::FRAME_FAST_MS))
                {
                    drawUI();
                }
            }
        }
    }

    void onDestroy() override
    {
        // 长按返回或系统切页可能在演出中离开；这条路径也要补存一次，不能丢弃已抽出的统计。
        savePendingStats();
    }

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
            // 绑定刚抽出的星级/瓦夜摘要，保留待抽布局和晃动时钟，避免按下后镜片突然换位。
            UIGacha::PullAppearance appearances[PULL_COUNT];
            for (int i = 0; i < PULL_COUNT; ++i)
            {
                if (!current_pulls[i].valid) continue;
                appearances[i].star = current_pulls[i].identity.star;
                appearances[i].walp = current_pulls[i].identity.walp == 1;
            }
            extraction_animation.SetPulls(appearances);
            m_scroll_offset = 0;
            phase = 1;
            anim_timer = millis();
            last_draw_time = anim_timer;
            drawUI(); // 从当前无色镜面继续演出，下一次短按仍直接跳过到这批结果。
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
            enterIdlePhase();
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
