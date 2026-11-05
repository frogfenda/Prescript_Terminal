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

    // 两种演出共用同一批身份值副本，翻面后露出打印清单。
    static constexpr int PULL_COUNT = UIGacha::PULL_COUNT;
    UIGacha::ExtractionAnimation extraction_animation;

    UIGacha::ResultReceipt result_receipt;
    uint32_t result_timer = 0;
    uint32_t result_entry_elapsed = UIGacha::ResultReceipt::ENTRY_MS;

    // ==========================================
    // 底层引擎：抽卡概率与数据分配
    // ==========================================
    bool rollSingle(bool is_guaranteed_2star, IdentityData &out)
    {
        if (SysIdentityCatalog::Count() == 0)
            return false;

        // 纯人格池（不含 E.G.O/播报员）：普通位三星 3%、二星 13%、一星 84%。
        // 保底位只把一星提升为二星，因此为三星 3%、二星 97%，不额外提高三星概率。
        const int r = random(1000);
        int target_star = 1;
        if (r < 30)
            target_star = 3;
        else if (r < 30 + 130)
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

        // 本应用只做十连：每批等概率选择一个保底位置，十个槽位都有 1/10 的机会。
        // 仍然是九个普通位 + 一个二星以上位，总体收益不变；不在抽完后追加抽取或改统计。
        const int guaranteed_index = (int)random(PULL_COUNT);
        for (int i = 0; i < PULL_COUNT; i++)
        {
            current_pulls[i].valid = rollSingle(i == guaranteed_index, current_pulls[i].identity);
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
        // 先聚焦本次最高稀有度的有效结果；同档保留抽取顺序，空资源仍可安全浏览十个槽位。
        int selected = 0, best_rank = -1;
        for (int i = 0; i < PULL_COUNT; ++i)
        {
            if (!current_pulls[i].valid) continue;
            const auto &id = current_pulls[i].identity;
            const int rank = id.star + (id.walp == 1 ? 3 : 0);
            if (rank > best_rank) { best_rank = rank; selected = i; }
        }
        result_receipt.Reset(selected, millis());
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
        const uint32_t now = millis();
        const uint32_t elapsed = now - anim_timer;
        // 自然结束接续翻面底下已经打印的纸面；主动跳过立即给出完整结果，不补播进场。
        result_entry_elapsed = elapsed >= extraction_animation.DurationMs()
            ? min(UIGacha::ResultReceipt::ENTRY_MS, elapsed - extraction_animation.FlipStartMs())
            : UIGacha::ResultReceipt::ENTRY_MS;
        result_timer = now;
        phase = 2;
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
        // 从翻面开始打印同一张正式清单，演出结束继续纸面时间轴，避免切换时重新打印。
        if (elapsed >= extraction_animation.FlipStartMs())
            drawResultPhase(sw, sh);
        extraction_animation.Draw(elapsed, now - idle_timer);
    }

    // ==========================================
    // UI 渲染：提取结算列表
    // ==========================================
    void drawResultPhase(int sw, int sh)
    {
        UIGacha::ReceiptRecord records[PULL_COUNT];
        for (int i = 0; i < PULL_COUNT; ++i)
        {
            if (!current_pulls[i].valid)
            {
                records[i].sinner = "--";
                continue;
            }
            const auto &id = current_pulls[i].identity;
            records[i].sinner = id.sinner.c_str();
            records[i].identity = id.id_name.c_str();
            records[i].appearance.star = (uint8_t)constrain(id.star, 0, 3);
            records[i].appearance.walp = id.walp == 1;
        }
        const uint32_t now = millis();
        const uint32_t entry = phase == 1 ? now - anim_timer - extraction_animation.FlipStartMs()
                                         : result_entry_elapsed + (now - result_timer);
        result_receipt.Draw(records, UIStrings::GachaReceiptTitle(appManager.getLanguage()),
                            sw, sh, entry, now);
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
        if (phase == 2)
        {
            const bool animating = result_receipt.NeedsFrame();
            if ((animating || result_receipt.HasLongDetail()) &&
                UIClock_Due(last_draw_time, animating ? UITheme::FRAME_FAST_MS : UITheme::FRAME_NORMAL_MS))
                drawUI();
        }
    }

    void onDestroy() override
    {
        // 长按返回或系统切页可能在演出中离开；这条路径也要补存一次，不能丢弃已抽出的统计。
        savePendingStats();
    }

    void onKnob(int delta) override
    {
        if (phase != 2 || delta == 0) return;
        const int selected = result_receipt.Selected();
        // 输入可能一次累计多格；先按边界限幅再相加，避免长整型极端值溢出。
        const int next = selected + constrain(delta, -selected, PULL_COUNT - 1 - selected);
        if (next == selected) return;
        result_receipt.Select(next, millis());
        SYS_SOUND_GLITCH();
        // 只更新目标，帧门控统一绘制；快速旋转不在回调里反复推整屏。
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
