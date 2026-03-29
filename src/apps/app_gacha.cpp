// 文件：src/apps/app_gacha.cpp
#include "app_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include "sys/sys_audio.h"
#include "sys_haptic.h"
#include "sys/sys_res.h"

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

    // 动画引擎：左到右线性揭晓
    bool revealed[10];                   // 记录每个方块是否已被扫描线扫过并揭晓
    const uint32_t ANIM_DURATION = 1800; // 扫描线扫过全屏的总时长 (1.8秒，节奏紧凑)

    // 列表引擎参数
    int m_scroll_offset = 0;
    const int MAX_VIS = 4;
    const int ROW_H = 18;

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

        for (int i = 0; i < 10; i++)
        {
            current_pulls[i].id_ptr = rollSingle(i == 9);
            current_pulls[i].is_new = false;
            if (current_pulls[i].id_ptr)
            {
                if (current_pulls[i].id_ptr->star > max_star_pulled)
                {
                    max_star_pulled = current_pulls[i].id_ptr->star;
                }
                if (current_pulls[i].id_ptr->walp == 1)
                {
                    has_walp_pulled = true;
                }
            }
            revealed[i] = false; // 初始状态全都不显示颜色
        }
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
        HAL_Screen_ShowChineseLine_Faded_Color(sw / 2 - 50, 20, ">> MEPHISTOPHELES", 0.0f, TFT_RED);
        HAL_Screen_ShowChineseLine_Faded_Color(sw / 2 - 65, 45, "[ 单击 ] 执行十连提取", 0.0f, TFT_WHITE);
    }

    // ==========================================
    // UI 渲染：【线性阵列解密】硬核扫描动画
    // ==========================================
    void drawAnimPhase(int sw, int sh)
    {
        uint32_t elapsed = millis() - anim_timer;

        // 顶部的暗色文字，留出下方完整的阵列空间
        HAL_Screen_ShowChineseLine_Faded_Color(sw / 2 - 40, 10, "EXTRACTION...", 0.0f, 0x18E3);

        // 1x10 横向阵列参数精算 (完美适配 284 宽度)
        int box_size = 18;                       // 方块大小
        int gap_x = 6;                           // 间距
        int total_w = 10 * box_size + 9 * gap_x; // 180 + 54 = 234 像素宽

        int start_x = (sw - total_w) / 2;
        int start_y = sh / 2 - box_size / 2 + 8; // 微微偏下放置阵列

        // 绘制 10 个方块
        for (int i = 0; i < 10; i++)
        {
            int x = start_x + i * (box_size + gap_x);
            int y = start_y;

            if (revealed[i])
            {
                // 如果扫描线已扫过，展现真实品级颜色
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
                HAL_Fill_Rect(x, y, box_size, box_size, box_color);
                HAL_Draw_Rect(x, y, box_size, box_size, TFT_WHITE); // 加亮边框
            }
            else
            {
                // 如果扫描线还没到，保持未激活的空心暗框
                HAL_Draw_Rect(x, y, box_size, box_size, 0x39E7); // 极暗的灰色
            }
        }

        // 绘制扫描线 (带有科幻的发光残影效果)
        if (elapsed < ANIM_DURATION)
        {
            int scan_x = start_x + (elapsed * total_w / ANIM_DURATION);
            HAL_Draw_Line(scan_x, start_y - 4, scan_x, start_y + box_size + 4, TFT_CYAN);
            HAL_Draw_Line(scan_x - 1, start_y - 2, scan_x - 1, start_y + box_size + 2, 0x03E0);
        }
    }

    // ==========================================
    // UI 渲染：提取结算列表
    // ==========================================
    void drawResultPhase(int sw, int sh)
    {
        for (int i = 0; i < MAX_VIS; i++)
        {
            int list_idx = m_scroll_offset + i;
            if (list_idx >= 10)
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
            int x_pos = (sw - 6 - text_w) / 2;
            if (x_pos < 2)
                x_pos = 2;

            int y_pos = 2 + i * ROW_H;
            HAL_Screen_ShowChineseLine_Faded_Color(x_pos, y_pos, line_text.c_str(), 0.0f, theme_color);
        }

        // 右侧滚轮进度条
        int max_offset = 10 - MAX_VIS;
        if (max_offset > 0)
        {
            int track_h = 64;
            int bar_h = track_h * MAX_VIS / 10;
            if (bar_h < 4)
                bar_h = 4;
            int bar_y = 6 + (track_h - bar_h) * m_scroll_offset / max_offset;

            HAL_Fill_Rect(sw - 6, bar_y, 3, bar_h, TFT_WHITE);
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
                int box_size = 18;
                int gap_x = 6;
                int total_w = 10 * box_size + 9 * gap_x;

                for (int i = 0; i < 10; i++)
                {
                    // 计算出扫描线扫到第 i 个方块“正中心”时所需的时间
                    int trigger_x = i * (box_size + gap_x) + box_size / 2;
                    uint32_t trigger_time = trigger_x * ANIM_DURATION / total_w;

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
            int max_offset = 10 - MAX_VIS;

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