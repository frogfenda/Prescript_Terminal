// 文件：src/apps/app_gacha.cpp
#include "app_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "sys/sys_audio.h"
#include "sys_haptic.h"
#include <new>

struct IdentityData
{
    String sinner;
    String id_name;
    int star;
    int walp;
};

struct PullResult
{
    IdentityData *id_ptr;
    bool is_new;
};

class AppGacha : public AppBase
{
private:
    static IdentityData *global_pool;
    static int pool_total;

    static int *pool_1star;
    static int count_1star;
    static int *pool_2star;
    static int count_2star;
    static int *pool_3star;
    static int count_3star;
    static bool is_loaded;

    PullResult current_pulls[10];
    int phase = 0;
    int cursor_idx = 0;
    uint32_t anim_timer = 0;
    int max_star_pulled = 1;
    void loadDatabase()
    {
        if (is_loaded)
            return;

        // 【核心修复】：精准定位到 assets 文件夹下的 json
        File file = LittleFS.open("/assets/ids.json", "r");
        if (!file)
        {
            Serial.println("[提取部] 致命错误：找不到 /assets/ids.json！");
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        if (error)
        {
            Serial.printf("[提取部] JSON 解析崩溃: %s\n", error.c_str());
            file.close();
            return;
        }

        pool_total = doc.size();
        if (pool_total == 0)
        {
            Serial.println("[提取部] JSON 数据为空或格式错误（可能没加中括号 []）！");
            file.close();
            return;
        }

        global_pool = (IdentityData *)ps_malloc(sizeof(IdentityData) * pool_total);
        pool_1star = (int *)ps_malloc(sizeof(int) * pool_total);
        pool_2star = (int *)ps_malloc(sizeof(int) * pool_total);
        pool_3star = (int *)ps_malloc(sizeof(int) * pool_total);

        count_1star = 0;
        count_2star = 0;
        count_3star = 0;

        for (int i = 0; i < pool_total; i++)
        {
            // 【致命崩溃修复 1】：在 ps_malloc 分配的原始内存上，强制调用 C++ 构造函数！
            new (&global_pool[i]) IdentityData();

            global_pool[i].sinner = doc[i]["sinner"].as<String>();
            global_pool[i].id_name = doc[i]["id"].as<String>();
            global_pool[i].star = doc[i]["star"].as<int>();
            global_pool[i].walp = doc[i]["walp"].as<int>();

            if (global_pool[i].star == 1)
                pool_1star[count_1star++] = i;
            else if (global_pool[i].star == 2)
                pool_2star[count_2star++] = i;
            else if (global_pool[i].star == 3)
                pool_3star[count_3star++] = i;
        }
        file.close();
        is_loaded = true;
        Serial.printf("[提取部] 数据重载完毕. 1★:%d | 2★:%d | 3★:%d\n", count_1star, count_2star, count_3star);
    }

    IdentityData *rollSingle(bool is_guaranteed_2star)
    {
        if (!is_loaded || pool_total == 0)
            return nullptr;

        int r = random(1000);
        int target_star = 1;

        if (r < 29)
            target_star = 3;
        else if (r < 29 + 128)
            target_star = 2;
        else if (is_guaranteed_2star)
            target_star = 2;

        if (target_star == 3 && count_3star == 0)
            target_star = 2;
        if (target_star == 2 && count_2star == 0)
            target_star = 1;

        // 【致命崩溃修复 2】：防除以零崩溃！如果 JSON 格式写错导致 1 星数量为 0，强制拿第一个数据兜底！
        if (target_star == 1 && count_1star == 0)
            return &global_pool[0];

        int selected_idx = 0;
        if (target_star == 3)
            selected_idx = pool_3star[random(count_3star)];
        else if (target_star == 2)
            selected_idx = pool_2star[random(count_2star)];
        else
            selected_idx = pool_1star[random(count_1star)];

        return &global_pool[selected_idx];
    }

    void executeTenPull()
    {
        max_star_pulled = 1;
        for (int i = 0; i < 10; i++)
        {
            bool guarantee = (i == 9);
            current_pulls[i].id_ptr = rollSingle(guarantee);
            current_pulls[i].is_new = false;

            if (current_pulls[i].id_ptr && current_pulls[i].id_ptr->star > max_star_pulled)
            {
                max_star_pulled = current_pulls[i].id_ptr->star;
            }
        }
    }

    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        if (phase == 0)
        {
            HAL_Screen_ShowChineseLine_Faded_Color(sw / 2 - 50, 20, ">> MEPHISTOPHELES", 0.0f, TFT_RED);
            HAL_Screen_ShowChineseLine_Faded_Color(sw / 2 - 65, 45, "[ 单击 ] 执行十连提取", 0.0f, TFT_WHITE);
        }
        else if (phase == 1)
        {
            uint16_t flash_color = TFT_WHITE;
            if (max_star_pulled == 3)
                flash_color = (millis() / 50) % 2 == 0 ? TFT_GOLD : TFT_YELLOW;
            else if (max_star_pulled == 2)
                flash_color = (millis() / 50) % 2 == 0 ? TFT_RED : TFT_MAROON;

            HAL_Screen_ShowChineseLine_Faded_Color(sw / 2 - 40, sh / 2 - 8, "EXTRACTION...", 0.0f, flash_color);
        }
        else if (phase == 2)
        {
            HAL_Draw_Line(120, 0, 120, sh, 0x18E3);

            // --- 渲染左侧详情 ---
            IdentityData *cur_id = current_pulls[cursor_idx].id_ptr;
            if (cur_id)
            {
                // 【色彩优先级引擎】：默认灰 -> 2星红 -> 3星金 -> 瓦尔普吉斯绿
                uint16_t theme_color = TFT_DARKGREY;
                String star_str = "★";
                if (cur_id->star == 2)
                {
                    theme_color = TFT_RED;
                    star_str = "★★";
                }
                if (cur_id->star == 3)
                {
                    theme_color = TFT_GOLD;
                    star_str = "★★★";
                }
                if (cur_id->walp == 1)
                {
                    theme_color = TFT_GREEN;
                } // 瓦尔普吉斯最高优先级覆盖

                HAL_Screen_ShowChineseLine_Faded_Color(4, 8, cur_id->sinner.c_str(), 0.0f, theme_color);
                HAL_Screen_ShowChineseLine_Faded_Color(4, 28, star_str.c_str(), 0.0f, theme_color);

                String short_name = cur_id->id_name;
                if (short_name.length() > 15)
                    short_name = short_name.substring(0, 15) + "..";
                // 名字也染上对应的品级/节日颜色，极具沉浸感
                HAL_Screen_ShowChineseLine_Faded_Color(4, 48, short_name.c_str(), 0.0f, theme_color);

                if (cur_id->walp == 1)
                {
                    HAL_Screen_ShowChineseLine_Faded_Color(90, 8, "W", 0.0f, TFT_GREEN);
                }
            }

            // --- 渲染右侧阵列 ---
            int start_x = 128;
            int start_y = 12;
            int gap_x = 30;
            int gap_y = 30;

            for (int i = 0; i < 10; i++)
            {
                int col = i % 5;
                int row = i / 5;
                int x = start_x + col * gap_x;
                int y = start_y + row * gap_y;

                IdentityData *p_id = current_pulls[i].id_ptr;
                if (!p_id)
                    continue;

                // 阵列方块同样的色彩优先逻辑
                uint16_t box_color = TFT_DARKGREY;
                if (p_id->star == 2)
                    box_color = TFT_RED;
                if (p_id->star == 3)
                    box_color = TFT_GOLD;
                if (p_id->walp == 1)
                    box_color = TFT_GREEN;

                if (i == cursor_idx)
                {
                    HAL_Draw_Line(x - 2, y - 2, x + 20, y - 2, TFT_WHITE);
                    HAL_Draw_Line(x - 2, y + 18, x + 20, y + 18, TFT_WHITE);
                    HAL_Draw_Line(x - 2, y - 2, x - 2, y + 18, TFT_WHITE);
                    HAL_Draw_Line(x + 20, y - 2, x + 20, y + 18, TFT_WHITE);
                }

                String initial = p_id->sinner;
                if (initial.length() >= 3 && (initial[0] & 0x80))
                    initial = initial.substring(0, 3);
                else
                    initial = initial.substring(0, 1);

                HAL_Screen_ShowChineseLine_Faded_Color(x, y, initial.c_str(), 0.0f, box_color);
            }
        }

        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        loadDatabase();
        phase = 0;
        cursor_idx = 0;
        drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override
    {
        if (phase == 1)
        {
            uint32_t now = millis();
            if (now - anim_timer > 1500)
            {
                phase = 2;
                if (max_star_pulled == 3)
                    sysAudio.playTone(2000, 300);
                else
                    sysAudio.playTone(1000, 100);
            }
            drawUI();
        }
    }

    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (phase == 2)
        {
            cursor_idx += delta;
            if (cursor_idx < 0)
                cursor_idx = 9;
            if (cursor_idx > 9)
                cursor_idx = 0;
            SYS_SOUND_GLITCH();
            drawUI();
        }
    }

    void onKeyShort() override
    {
        if (phase == 0 || phase == 2)
        {
            SYS_SOUND_CONFIRM();
            executeTenPull();
            phase = 1;
            anim_timer = millis();
        }
    }

    void onKeyLong() override
    {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

IdentityData *AppGacha::global_pool = nullptr;
int AppGacha::pool_total = 0;
int *AppGacha::pool_1star = nullptr;
int AppGacha::count_1star = 0;
int *AppGacha::pool_2star = nullptr;
int AppGacha::count_2star = 0;
int *AppGacha::pool_3star = nullptr;
int AppGacha::count_3star = 0;
bool AppGacha::is_loaded = false;

AppGacha instanceGacha;
AppBase *appGacha = &instanceGacha;