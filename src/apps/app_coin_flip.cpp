// 文件：src/apps/app_coin_flip.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>
#include "sys/sys_config.h"
#include "sys/sys_audio.h"

const int SRC_COIN_SIZE = 64;

namespace CoinAnimParams
{
    const float PERSPECTIVE_SCALE = 1.0f;
    const uint32_t FRAME_DELAY_MS = 16;
    const float RAPID_SPIN_STEP = 0.6f;

    const int AUTO_SPIN_INITIAL = 45;
    const int AUTO_SPIN_INTERVAL = 20;
    const int FLASH_DURATION = 6;
}

struct CoinEntity
{
    float current_angle;
    bool is_flipping;
    int auto_stop_timer;
    int target_face;
    int flash_frames;
    bool needs_redraw;
};

class AppCoinAnim : public AppBase
{
private:
    uint16_t *img_heads = nullptr;
    uint16_t *img_tails = nullptr;
    uint16_t *coin_buffer = nullptr;

    uint8_t *wav_heads_data = nullptr;
    uint32_t wav_heads_len = 0;

    uint8_t *wav_tails_data = nullptr;
    uint32_t wav_tails_len = 0;

    CoinEntity coins[9]; // 【封印彻底解除】：支持 9 枚实体！
    int active_coins = 1;
    int current_coin_size = SRC_COIN_SIZE;

    bool global_is_animating = false;
    uint32_t last_frame_time = 0;

    void loadCoinData()
    {
        if (!img_heads)
            img_heads = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
        if (!img_tails)
            img_tails = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
        if (!coin_buffer)
            coin_buffer = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);

        // ==========================================
        // 【智能材质拼装与兜底机制】
        // 自动拼装 rheads.bin 或 gheads.bin，如果找不到，自动降级用经典款
        // ==========================================
        String prefix = "/assets/coins/";
        if (sysConfig.coin_data.coin_type == 1)
            prefix += "r";
        else if (sysConfig.coin_data.coin_type == 2)
            prefix += "g";

        String path_h_bin = prefix + "heads.bin";
        String path_t_bin = prefix + "tails.bin";
        String path_h_wav = prefix + "heads.wav";
        String path_t_wav = prefix + "tails.wav";

        File f1 = LittleFS.open(path_h_bin.c_str(), "r");
        if (!f1)
            f1 = LittleFS.open("/assets/coins/heads.bin", "r"); // 兜底保护
        if (f1)
        {
            f1.read((uint8_t *)img_heads, SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
            f1.close();
        }

        File f2 = LittleFS.open(path_t_bin.c_str(), "r");
        if (!f2)
            f2 = LittleFS.open("/assets/coins/tails.bin", "r");
        if (f2)
        {
            f2.read((uint8_t *)img_tails, SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
            f2.close();
        }

       
    }

    void drawScaledCoinToBuffer(int idx, float scaleX, int target_size)
    {
        memset(coin_buffer, 0, target_size * target_size * 2);

        uint16_t *img = (scaleX >= 0) ? img_heads : img_tails;
        if (!img)
            return;

        bool is_spinning = coins[idx].is_flipping;
        bool is_flashing = (coins[idx].flash_frames > 0);
        bool is_dimmed = (!is_spinning && coins[idx].target_face == 1);

        int drawW = target_size * fabs(scaleX) * CoinAnimParams::PERSPECTIVE_SCALE;
        if (drawW % 2 != 0)
            drawW++;
        if (drawW <= 1)
            return;

        int startX = (target_size - drawW) / 2;
        bool is_back = (scaleX < 0);

        uint8_t x_map[SRC_COIN_SIZE];
        for (int x = 0; x < drawW; x++)
        {
            int origX = x * (SRC_COIN_SIZE - 1) / (drawW - 1);
            if (is_back)
                origX = (SRC_COIN_SIZE - 1) - origX;
            x_map[x] = origX;
        }

        for (int dst_y = 0; dst_y < target_size; dst_y++)
        {
            int src_y = dst_y * SRC_COIN_SIZE / target_size;
            if (src_y >= SRC_COIN_SIZE)
                src_y = SRC_COIN_SIZE - 1;

            int src_y_offset = src_y * SRC_COIN_SIZE;
            int dst_y_offset = dst_y * target_size;

            for (int dst_x = 0; dst_x < drawW; dst_x++)
            {
                uint16_t color = img[src_y_offset + x_map[dst_x]];

                if (color != 0x0000)
                {
                    if (is_flashing && !is_back && !is_spinning)
                    {
                        uint32_t intensity = (coins[idx].flash_frames * 256) / CoinAnimParams::FLASH_DURATION;
                        uint16_t r = (color >> 11) & 0x1F;
                        uint16_t g = (color >> 5) & 0x3F;
                        uint16_t b = color & 0x1F;
                        r = r + (((31 - r) * intensity) >> 8);
                        g = g + (((63 - g) * intensity) >> 8);
                        b = b + (((31 - b) * intensity) >> 8);
                        color = (r << 11) | (g << 5) | b;
                    }
                    else if (is_dimmed && !is_spinning)
                    {
                        uint16_t r = ((color >> 11) & 0x1F) * 80 / 100;
                        uint16_t g = ((color >> 5) & 0x3F) * 80 / 100;
                        uint16_t b = (color & 0x1F) * 80 / 100;
                        color = (r << 11) | (g << 5) | b;
                    }
                    coin_buffer[dst_y_offset + startX + dst_x] = color;
                }
            }
        }
    }

    void drawStaticUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        if (sysConfig.coin_data.sanity != 0)
        {
            char san_str[16];
            sprintf(san_str, "%+d", sysConfig.coin_data.sanity);
            uint16_t color = (sysConfig.coin_data.sanity < 0) ? 0xF800 : 0x07FF;
            int tw = HAL_Get_Text_Width(san_str);
            HAL_Screen_ShowChineseLine_Faded_Color(sw - tw - 8, sh - 18, san_str, 0.0f, color);
        }
        HAL_Screen_Update();
    }

    void drawActiveCoinsOnly()
    {
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        int min_gap = 4;
        int max_possible_size = (sw - (active_coins + 1) * min_gap) / active_coins;
        int max_h = (sh > SRC_COIN_SIZE) ? SRC_COIN_SIZE : sh - 4;

        current_coin_size = (max_possible_size < max_h) ? max_possible_size : max_h;
        if (current_coin_size > SRC_COIN_SIZE)
            current_coin_size = SRC_COIN_SIZE;

        int actual_gap = (sw - (active_coins * current_coin_size)) / (active_coins + 1);
        int start_y = (sh - current_coin_size) / 2;

        for (int i = 0; i < active_coins; i++)
        {
            if (coins[i].needs_redraw || coins[i].is_flipping || coins[i].flash_frames > 0)
            {
                float scale = cos(coins[i].current_angle);

                drawScaledCoinToBuffer(i, scale, current_coin_size);
                int target_x = actual_gap + i * (current_coin_size + actual_gap);

                HAL_Sprite_PushImage(target_x, start_y, current_coin_size, current_coin_size, coin_buffer);
                HAL_Screen_Update_Area(target_x, start_y, current_coin_size, current_coin_size);

                coins[i].needs_redraw = false;
            }
        }
    }

    void stopCoinInstantly(int idx)
    {
        coins[idx].is_flipping = false;

        int heads_chance = 50 + sysConfig.coin_data.sanity;
        int roll = random(100);
        coins[idx].target_face = (roll < heads_chance) ? 0 : 1;
        coins[idx].current_angle = (coins[idx].target_face == 0) ? 0.0f : PI;
        coins[idx].needs_redraw = true;

        sysAudio.stopWAV();

        if (coins[idx].target_face == 0)
        {
            coins[idx].flash_frames = CoinAnimParams::FLASH_DURATION;
            if (wav_heads_data)
                sysAudio.playWAV(g_wav_heads, g_wav_heads_len);
            else
                SYS_SOUND_CONFIRM();
        }
        else
        {
            if (wav_tails_data)
                sysAudio.playWAV(g_wav_tails, g_wav_tails_len);
            else
                SYS_SOUND_NAV();
        }
    }

public:
    void onCreate() override
    {
        loadCoinData();
        active_coins = sysConfig.coin_data.coin_count;
        if (active_coins < 1)
            active_coins = 1;
        if (active_coins > 9)
            active_coins = 9;

        global_is_animating = false;
        for (int i = 0; i < active_coins; i++)
        {
            coins[i].current_angle = 0;
            coins[i].is_flipping = false;
            coins[i].target_face = 0;
            coins[i].flash_frames = 0;
            coins[i].needs_redraw = true;
        }
        drawStaticUI();
        drawActiveCoinsOnly();
    }

    void onResume() override
    {
        for (int i = 0; i < active_coins; i++)
            coins[i].needs_redraw = true;
        drawStaticUI();
        drawActiveCoinsOnly();
    }

    void onLoop() override
    {
        if (global_is_animating)
        {
            uint32_t now = millis();
            if (now - last_frame_time < CoinAnimParams::FRAME_DELAY_MS)
                return;
            last_frame_time = now;

            bool any_active = false;

            for (int i = 0; i < active_coins; i++)
            {
                if (coins[i].is_flipping)
                {
                    any_active = true;
                    coins[i].current_angle += CoinAnimParams::RAPID_SPIN_STEP;

                    if (sysConfig.coin_data.mode == 0)
                    {
                        if (coins[i].auto_stop_timer > 0)
                            coins[i].auto_stop_timer--;
                        else
                            stopCoinInstantly(i);
                    }
                }

                if (coins[i].flash_frames > 0)
                {
                    any_active = true;
                    coins[i].flash_frames--;
                    if (coins[i].flash_frames == 0)
                        coins[i].needs_redraw = true;
                }
            }

            drawActiveCoinsOnly();
            if (!any_active)
                global_is_animating = false;
        }
    }

    void onDestroy() override
    {
        sysAudio.stopWAV();

        if (img_heads)
        {
            free(img_heads);
            img_heads = nullptr;
        }
        if (img_tails)
        {
            free(img_tails);
            img_tails = nullptr;
        }
        if (coin_buffer)
        {
            free(coin_buffer);
            coin_buffer = nullptr;
        }

 
    }

    void onKnob(int delta) override {}

    void onKeyShort() override
    {
        if (global_is_animating)
        {
            if (sysConfig.coin_data.mode == 1)
            {
                for (int i = 0; i < active_coins; i++)
                {
                    if (coins[i].is_flipping)
                    {
                        stopCoinInstantly(i);
                        break;
                    }
                }
            }
            return;
        }

        sysAudio.stopWAV();
        SYS_SOUND_CONFIRM();
        global_is_animating = true;

        for (int i = 0; i < active_coins; i++)
        {
            coins[i].is_flipping = true;
            coins[i].flash_frames = 0;

            if (sysConfig.coin_data.mode == 0)
            {
                coins[i].auto_stop_timer = CoinAnimParams::AUTO_SPIN_INITIAL + (i * CoinAnimParams::AUTO_SPIN_INTERVAL);
            }
        }
        last_frame_time = millis();
    }

    void onKeyLong() override
    {

            SYS_SOUND_NAV();
            appManager.popApp();
    }
};

AppCoinAnim instanceCoinAnim;

// ==========================================
// 【终端模块设置界面】
// ==========================================
class AppCoinSettings : public AppMenuBase
{
private:
    bool is_editing = false;

protected:
    int getMenuCount() override { return 4; } // 【修改】：选项由 3 个变为 4 个

    const char *getTitle() override
    {
        return appManager.getLanguage() == LANG_ZH ? ">> 决策参数设置" : ">> FLIP SETTINGS";
    }

    const char *getItemText(int index) override
    {
        static char text_buf[64];
        bool zh = appManager.getLanguage() == LANG_ZH;
        const char *cursor = (index == current_selection) ? "> " : "  ";

        if (index == 0)
            sprintf(text_buf, zh ? "%s运行模式 [ %s ]" : "%sMODE: [ %s ]", cursor, sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL"));
        else if (index == 1)
            sprintf(text_buf, zh ? "%s理智波动 [ %+d ]" : "%sSANITY: [ %+d ]", cursor, sysConfig.coin_data.sanity);
        else if (index == 2)
            sprintf(text_buf, zh ? "%s阵列数量 [ %d ]" : "%sCOINS: [ %d ]", cursor, sysConfig.coin_data.coin_count);
        else if (index == 3)
        {
            // 【新增】：根据 0/1/2 返回不同的酷炫名称
            const char *type_str;
            if (sysConfig.coin_data.coin_type == 1)
                type_str = zh ? "狂气红" : "RED";
            else if (sysConfig.coin_data.coin_type == 2)
                type_str = zh ? "沉稳绿" : "GREEN";
            else
                type_str = zh ? "经典金" : "GOLD";
            sprintf(text_buf, zh ? "%s硬币型号 [ %s ]" : "%sTYPE: [ %s ]", cursor, type_str);
        }
        return text_buf;
    }

    bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix) override
    {
        if (!is_editing || index != current_selection)
            return false;
        static char val_str[16];
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0)
        {
            *prefix = zh ? ">> 运行模式 [ " : ">> MODE: [ ";
            *anim_val = sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL");
            *suffix = " ]";
            return true;
        }
        else if (index == 1)
        {
            *prefix = zh ? ">> 理智波动 [ " : ">> SANITY: [ ";
            sprintf(val_str, "%+d", sysConfig.coin_data.sanity);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        else if (index == 2)
        {
            *prefix = zh ? ">> 阵列数量 [ " : ">> COINS: [ ";
            sprintf(val_str, "%d", sysConfig.coin_data.coin_count);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        else if (index == 3)
        { // 【新增修改态 UI】
            *prefix = zh ? ">> 硬币型号 [ " : ">> TYPE: [ ";
            if (sysConfig.coin_data.coin_type == 1)
                *anim_val = zh ? "狂气红" : "RED";
            else if (sysConfig.coin_data.coin_type == 2)
                *anim_val = zh ? "沉稳绿" : "GREEN";
            else
                *anim_val = zh ? "经典金" : "GOLD";
            *suffix = " ]";
            return true;
        }
        return false;
    }

    uint16_t getItemColor(int index) override
    {
        if (index == current_selection)
            return is_editing ? 0x1C9F : 0xFFFF;
        return 0x07FF;
    }

    void onItemClicked(int index) override
    {
        SYS_SOUND_CONFIRM();
        is_editing = !is_editing;
        if (!is_editing)
            sysConfig.save();
        AppMenuBase::onResume();
    }

    void onKnob(int delta) override
    {
        if (is_editing)
        {
            triggerEditAnimation(delta);
            if (current_selection == 0)
                sysConfig.coin_data.mode = sysConfig.coin_data.mode == 0 ? 1 : 0;
            else if (current_selection == 1)
            {
                sysConfig.coin_data.sanity += delta;
                if (sysConfig.coin_data.sanity > 45)
                    sysConfig.coin_data.sanity = 45;
                if (sysConfig.coin_data.sanity < -45)
                    sysConfig.coin_data.sanity = -45;
            }
            else if (current_selection == 2)
            {
                sysConfig.coin_data.coin_count += delta;
                if (sysConfig.coin_data.coin_count > 9)
                    sysConfig.coin_data.coin_count = 9;
                if (sysConfig.coin_data.coin_count < 1)
                    sysConfig.coin_data.coin_count = 1;
            }
            else if (current_selection == 3)
            { // 【新增逻辑】
                sysConfig.coin_data.coin_type += delta;
                if (sysConfig.coin_data.coin_type > 2)
                    sysConfig.coin_data.coin_type = 2;
                if (sysConfig.coin_data.coin_type < 0)
                    sysConfig.coin_data.coin_type = 0;
            }
        }
        else
            AppMenuBase::onKnob(delta);
    }

    void onLongPressed() override
    {
        if (is_editing)
        {
            is_editing = false;
            SYS_SOUND_CONFIRM();
            sysConfig.save();
            AppMenuBase::onResume();
        }
        else
        {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};
AppCoinSettings instanceCoinSettings;

// ==========================================
// 【系统主入口】
// ==========================================
class AppCoinMenu : public AppMenuBase
{
protected:
    int getMenuCount() override { return 2; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? ">> 量子决策模块" : ">> QUANTUM FLIP"; }
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        switch (index)
        {
        case 0:
            return zh ? "开始推演协议" : "START PROTOCOL";
        case 1:
            return zh ? "终端模块设置" : "MODULE SETTINGS";
        }
        return "";
    }
    void onItemClicked(int index) override
    {
        if (index == 0)
            appManager.pushApp(&instanceCoinAnim);
        else if (index == 1)
            appManager.pushApp(&instanceCoinSettings);
    }
    void onLongPressed() override
    {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppCoinMenu instanceCoinMenu;
AppBase *appCoinFlip = &instanceCoinMenu;