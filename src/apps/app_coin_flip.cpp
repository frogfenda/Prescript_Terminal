// 文件：src/apps/app_coin_flip.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>
#include "sys/sys_config.h"

#define COIN_SIZE 64

// ==========================================
// 【动画参数调优引擎】
// ==========================================
namespace CoinAnimParams
{
    const float PERSPECTIVE_SCALE = 1.0f;

    // 维持 47FPS (21ms) 避开 ST7789 屏幕 60Hz 扫描频闪共振
    const uint32_t FRAME_DELAY_MS = 16;

    // 【旋转质感调优】：0.55f 约等于每帧转 31 度。
    // 这个速度能让硬币在极速狂转时，依然保留清晰的“变窄 -> 翻面”的真实 3D 视觉过程。
    const float RAPID_SPIN_STEP = 0.6f;

    const int AUTO_SPIN_INITIAL = 4;
    const int AUTO_SPIN_INTERVAL = 10;
    const int FLASH_DURATION = 6;
}

// ==========================================
// 【多硬币物理实体阵列】
// ==========================================
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

    // 【新增】：PSRAM 音频内存物理指针
    uint8_t *wav_heads_data = nullptr;
    uint32_t wav_heads_len = 0;

    uint8_t *wav_tails_data = nullptr;
    uint32_t wav_tails_len = 0;

    CoinEntity coins[4];
    int active_coins = 1;
    bool global_is_animating = false;
    uint32_t last_frame_time = 0;

    void loadCoinData()
    {
        // 1. 分配并读取图片数据（放在内部高速 RAM）
        if (!img_heads)
            img_heads = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!img_tails)
            img_tails = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!coin_buffer)
            coin_buffer = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);

        File f1 = LittleFS.open("/assets/coins/heads.bin", "r");
        if (f1)
        {
            f1.read((uint8_t *)img_heads, COIN_SIZE * COIN_SIZE * 2);
            f1.close();
        }

        File f2 = LittleFS.open("/assets/coins/tails.bin", "r");
        if (f2)
        {
            f2.read((uint8_t *)img_tails, COIN_SIZE * COIN_SIZE * 2);
            f2.close();
        }

        // ==========================================
        // 2. 【核心魔法】：将硬盘里的 WAV 瞬间吸入 PSRAM 高速外部内存！
        // ==========================================
        File f_hw = LittleFS.open("/assets/coins/heads.wav", "r");
        if (f_hw)
        {
            // 减去 44 字节的标准 WAV 文件头，只保留纯净的 PCM 音频波形
            wav_heads_len = f_hw.size() - 44;
            // 使用 ps_malloc 分配外部 PSRAM，绝不挤占宝贵的内部内存！
            wav_heads_data = (uint8_t *)ps_malloc(wav_heads_len);
            if (wav_heads_data)
            {
                f_hw.seek(44);                            // 跳过文件头
                f_hw.read(wav_heads_data, wav_heads_len); // 一口吸入内存
            }
            f_hw.close();
        }

        File f_tw = LittleFS.open("/assets/coins/tails.wav", "r");
        if (f_tw)
        {
            wav_tails_len = f_tw.size() - 44;
            wav_tails_data = (uint8_t *)ps_malloc(wav_tails_len);
            if (wav_tails_data)
            {
                f_tw.seek(44);
                f_tw.read(wav_tails_data, wav_tails_len);
            }
            f_tw.close();
        }
    }

    void drawScaledCoinToBuffer(int idx, float scaleX)
    {
        memset(coin_buffer, 0, COIN_SIZE * COIN_SIZE * 2);

        // 【真实材质】：根据 scaleX 的正负，决定是从正面图还是反面图取像素！
        uint16_t *img = (scaleX >= 0) ? img_heads : img_tails;
        if (!img)
            return;

        bool is_spinning = coins[idx].is_flipping;
        bool is_flashing = (coins[idx].flash_frames > 0);
        bool is_dimmed = (!is_spinning && coins[idx].target_face == 1);

        int drawW = COIN_SIZE * fabs(scaleX) * CoinAnimParams::PERSPECTIVE_SCALE;
        if (drawW % 2 != 0)
            drawW++;
        if (drawW <= 1)
            return;

        int startX = (COIN_SIZE - drawW) / 2;
        bool is_back = (scaleX < 0);
        uint8_t x_map[COIN_SIZE];

        for (int x = 0; x < drawW; x++)
        {
            int origX = x * (COIN_SIZE - 1) / (drawW - 1);
            if (is_back)
                origX = (COIN_SIZE - 1) - origX;
            x_map[x] = origX;
        }

        for (int y = 0; y < COIN_SIZE; y++)
        {
            int y_offset = y * COIN_SIZE;
            for (int x = 0; x < drawW; x++)
            {
                uint16_t color = img[y_offset + x_map[x]];
                if (color != 0x0000)
                {
                    if (is_flashing && !is_back && !is_spinning)
                    {
                        // 【优化】：高能过曝衰减算法
                        // 利用剩余的 flash_frames 计算当前的闪光强度 (256 为满强度)
                        uint32_t intensity = (coins[idx].flash_frames * 256) / CoinAnimParams::FLASH_DURATION;

                        // 提取原图的 RGB565 分量
                        uint16_t r = (color >> 11) & 0x1F;
                        uint16_t g = (color >> 5) & 0x3F;
                        uint16_t b = color & 0x1F;

                        // 将原图色彩向纯白 (31, 63, 31) 进行线性差值逼近
                        // 强度越高，颜色越接近纯白；随着帧数递减，颜色极其丝滑地回归原图
                        r = r + (((31 - r) * intensity) >> 8);
                        g = g + (((63 - g) * intensity) >> 8);
                        b = b + (((31 - b) * intensity) >> 8);

                        color = (r << 11) | (g << 5) | b;
                    }
                    else if (is_dimmed && !is_spinning)
                    {
                        // 失败反面：压暗至 30% 亮度
                        uint16_t r = ((color >> 11) & 0x1F) * 80 / 100;
                        uint16_t g = ((color >> 5) & 0x3F) * 80 / 100;
                        uint16_t b = (color & 0x1F) * 80 / 100;
                        color = (r << 11) | (g << 5) | b;
                    }

                    coin_buffer[y_offset + startX + x] = color;
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

    // 零撕裂脏矩形推送引擎
    void drawActiveCoinsOnly()
    {
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        int gap = (sw - (active_coins * COIN_SIZE)) / (active_coins + 1);
        int start_y = (sh - COIN_SIZE) / 2;

        for (int i = 0; i < active_coins; i++)
        {
            if (coins[i].needs_redraw || coins[i].is_flipping || coins[i].flash_frames > 0)
            {
                float scale = cos(coins[i].current_angle);
                drawScaledCoinToBuffer(i, scale);

                int target_x = gap + i * (COIN_SIZE + gap);

                HAL_Sprite_PushImage(target_x, start_y, COIN_SIZE, COIN_SIZE, coin_buffer);
                HAL_Screen_Update_Area(target_x, start_y, COIN_SIZE, COIN_SIZE);

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

        // 瞬间拍平：正面(0.0) 或 反面(PI)
        coins[idx].current_angle = (coins[idx].target_face == 0) ? 0.0f : PI;
        coins[idx].needs_redraw = true;

        if (coins[idx].target_face == 0)
        {
            coins[idx].flash_frames = CoinAnimParams::FLASH_DURATION;

            // 【降维打击】：正面落地，直接把 PSRAM 里的音频块砸给功放！
            if (wav_heads_data)
            {
                sysAudio.playWAV(wav_heads_data, wav_heads_len);
            }
            else
            {
                SYS_SOUND_CONFIRM(); // 如果文件没找到，降级回电子音防奔溃
            }
        }
        else
        {
            // 反面落地
            if (wav_tails_data)
            {
                sysAudio.playWAV(wav_tails_data, wav_tails_len);
            }
            else
            {
                SYS_SOUND_NAV();
            }
        }
    }

public:
    void onCreate() override
    {
        loadCoinData();
        active_coins = sysConfig.coin_data.coin_count;
        if (active_coins < 1)
            active_coins = 1;
        if (active_coins > 4)
            active_coins = 4;

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
                    // 【真实 3D 旋转累加】
                    coins[i].current_angle += CoinAnimParams::RAPID_SPIN_STEP;

                    if (sysConfig.coin_data.mode == 0)
                    {
                        if (coins[i].auto_stop_timer > 0)
                        {
                            coins[i].auto_stop_timer--;
                        }
                        else
                        {
                            stopCoinInstantly(i);
                        }
                    }
                }

                if (coins[i].flash_frames > 0)
                {
                    any_active = true;
                    coins[i].flash_frames--;
                    if (coins[i].flash_frames == 0)
                    {
                        coins[i].needs_redraw = true;
                    }
                }
            }

            drawActiveCoinsOnly();

            if (!any_active)
            {
                global_is_animating = false;
            }
        }
    }

    void onDestroy() override
    {
        // 退出界面时，必须严格释放内存，防止内存泄漏！
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

        // 【新增】：释放 PSRAM 音频内存
        if (wav_heads_data)
        {
            free(wav_heads_data);
            wav_heads_data = nullptr;
        }
        if (wav_tails_data)
        {
            free(wav_tails_data);
            wav_tails_data = nullptr;
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
        if (!global_is_animating)
        {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
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
    int getMenuCount() override { return 3; }

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
        return false;
    }

    uint16_t getItemColor(int index) override
    {
        if (index == current_selection)
        {
            if (is_editing)
                return 0x1C9F;
            return 0xFFFF;
        }
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
                if (sysConfig.coin_data.coin_count > 4)
                    sysConfig.coin_data.coin_count = 4;
                if (sysConfig.coin_data.coin_count < 1)
                    sysConfig.coin_data.coin_count = 1;
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