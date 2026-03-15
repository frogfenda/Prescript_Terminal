// 文件：src/apps/app_coin_flip.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>
#include "sys/sys_config.h"

#define COIN_SIZE 64

// ==========================================
// 【动画参数调优区】 (修改这些系数来微调物理与视觉手感)
// ==========================================
namespace CoinAnimParams {
    // --- 视觉形变 ---
    const float PERSPECTIVE_SCALE = 1.0f;  // 硬币宽度的缩放倍率 (控制 cos 函数的振幅，1.0为标准圆，值越小显得越扁)
    
    // --- 引擎帧率 ---
    const uint32_t FRAME_DELAY_MS = 16;    // 每帧间隔时间 (16ms 约等于 60FPS)

    // --- 旋转圈数 (以半圈/PI为单位) ---
    const int AUTO_BASE_SPINS = 6;         // 自动模式：基础旋转圈数
    const int MANUAL_STOP_SPINS = 4;       // 手动模式：按下刹车后，还需要转几圈才停下
    const int EXTRA_SPINS_PER_COIN = 2;    // 老虎机视效：从左到右，下一枚硬币比上一枚多转几圈

    // --- 角速度与阻尼系数 (决定旋转的力学质感) ---
    const float MANUAL_SPIN_SPEED = 0.16f; // 手动模式：等待落锤时的狂转恒定速度 (弧度/帧)
    
    const float BASE_DAMPING = 0.04f;      // 自动/刹车模式：第一枚硬币的基础阻尼系数 (越小滑行越远，越大刹车越急)
    const float DAMPING_DIFF = 0.003f;     // 自动/刹车模式：后续硬币依次递减的阻尼量 (制造错落停顿感)
    
    const float MAX_AUTO_SPEED = 0.18f;    // 极限速度钳制：防止起步转速过高引发屏幕撕裂与频闪
    const float MIN_STOP_SPEED = 0.03f;    // 怠速钳制：防止最后阶段速度衰减过大导致硬币卡住转不到位
}

// ==========================================
// 【多硬币物理实体阵列】
// ==========================================
struct CoinEntity {
    float current_angle;
    float target_angle;
    bool is_flipping;
    bool is_stopping;
    int last_half_turn;
    float step_speed; // 独立阻尼系数
};

class AppCoinAnim : public AppBase {
private:
    uint16_t *img_heads = nullptr;
    uint16_t *img_tails = nullptr;
    uint16_t *coin_buffer = nullptr;

    CoinEntity coins[4]; 
    int active_coins = 1;
    bool global_is_flipping = false;
    uint32_t last_frame_time = 0;

    void loadCoinData() {
        if (!img_heads) img_heads = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!img_tails) img_tails = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!coin_buffer) coin_buffer = (uint16_t *)malloc(COIN_SIZE * COIN_SIZE * 2);

        File f1 = LittleFS.open("/assets/coins/heads.bin", "r");
        if (f1) { f1.read((uint8_t *)img_heads, COIN_SIZE * COIN_SIZE * 2); f1.close(); }

        File f2 = LittleFS.open("/assets/coins/tails.bin", "r");
        if (f2) { f2.read((uint8_t *)img_tails, COIN_SIZE * COIN_SIZE * 2); f2.close(); }
    }

    void drawScaledCoinToBuffer(float scaleX) {
        memset(coin_buffer, 0, COIN_SIZE * COIN_SIZE * 2);

        uint16_t *img = (scaleX >= 0) ? img_heads : img_tails;
        if (!img) return;

        // 【应用视觉系数】：在此处注入缩放倍率
        int drawW = COIN_SIZE * fabs(scaleX) * CoinAnimParams::PERSPECTIVE_SCALE;
        
        if (drawW % 2 != 0) drawW++;
        if (drawW <= 1) return; // 极窄虚空隐身

        int startX = (COIN_SIZE - drawW) / 2;
        bool is_back = (scaleX < 0);
        uint8_t x_map[COIN_SIZE];

        for (int x = 0; x < drawW; x++) {
            int origX = x * (COIN_SIZE - 1) / (drawW - 1);
            if (is_back) origX = (COIN_SIZE - 1) - origX;
            x_map[x] = origX;
        }

        for (int y = 0; y < COIN_SIZE; y++) {
            int y_offset = y * COIN_SIZE;
            for (int x = 0; x < drawW; x++) {
                uint16_t color = img[y_offset + x_map[x]];
                if (color != 0x0000) {
                    coin_buffer[y_offset + startX + x] = color;
                }
            }
        }
    }

    void drawUI() {
        HAL_Sprite_Clear();

        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        
        int gap = (sw - (active_coins * COIN_SIZE)) / (active_coins + 1);
        int start_y = (sh - COIN_SIZE) / 2;

        for (int i = 0; i < active_coins; i++) {
            float scale = cos(coins[i].current_angle);
            drawScaledCoinToBuffer(scale);
            
            int target_x = gap + i * (COIN_SIZE + gap);
            HAL_Sprite_PushImage(target_x, start_y, COIN_SIZE, COIN_SIZE, coin_buffer);
        }

        if (sysConfig.coin_data.sanity != 0) {
            char san_str[16];
            sprintf(san_str, "%+d", sysConfig.coin_data.sanity); 
            
            uint16_t color = (sysConfig.coin_data.sanity < 0) ? 0xF800 : 0x07FF; 
            
            int tw = HAL_Get_Text_Width(san_str);
            HAL_Screen_ShowChineseLine_Faded_Color(sw - tw - 8, sh - 18, san_str, 0.0f, color);
        }

        HAL_Screen_Update();
    }

    void rollCoinWithSanity(int idx, int base_spins) {
        int heads_chance = 50 + sysConfig.coin_data.sanity; 
        int roll = random(100);                       
        int target_face = (roll < heads_chance) ? 0 : 1;

        int spins = base_spins + random(0, 3); 
        int current_n = round(coins[idx].current_angle / PI);
        int final_n = current_n + spins;

        if (target_face == 0 && (final_n % 2 != 0)) final_n++;
        if (target_face == 1 && (final_n % 2 == 0)) final_n++;

        coins[idx].target_angle = final_n * PI;
    }

public:
    void onCreate() override {
        loadCoinData();
        active_coins = sysConfig.coin_data.coin_count;
        if (active_coins < 1) active_coins = 1;
        if (active_coins > 4) active_coins = 4;

        global_is_flipping = false;
        for (int i = 0; i < active_coins; i++) {
            coins[i].current_angle = 0;
            coins[i].target_angle = 0;
            coins[i].is_flipping = false;
            coins[i].is_stopping = false;
            coins[i].last_half_turn = 0;
            
            // 【应用阻尼系数】
            coins[i].step_speed = CoinAnimParams::BASE_DAMPING - (i * CoinAnimParams::DAMPING_DIFF); 
        }
        drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override {
        if (global_is_flipping) {
            uint32_t now = millis();
            
            // 【应用帧率控制】
            if (now - last_frame_time < CoinAnimParams::FRAME_DELAY_MS) return; 
            last_frame_time = now;

            bool any_still_flipping = false;
            bool played_nav_sound = false;
            bool played_confirm_sound = false;

            for (int i = 0; i < active_coins; i++) {
                if (!coins[i].is_flipping) continue;
                any_still_flipping = true;

                float step = 0;
                if (sysConfig.coin_data.mode == 1 && !coins[i].is_stopping) {
                    // 【应用手动狂转速度】
                    step = CoinAnimParams::MANUAL_SPIN_SPEED; 
                } else {
                    float dist = coins[i].target_angle - coins[i].current_angle;
                    step = dist * coins[i].step_speed; 
                    
                    // 【应用极限速度钳制】
                    if (step > CoinAnimParams::MAX_AUTO_SPEED) step = CoinAnimParams::MAX_AUTO_SPEED; 
                    if (step < CoinAnimParams::MIN_STOP_SPEED) step = CoinAnimParams::MIN_STOP_SPEED;
                }

                coins[i].current_angle += step;

                if ((sysConfig.coin_data.mode == 0 || coins[i].is_stopping) && coins[i].current_angle >= coins[i].target_angle) {
                    coins[i].current_angle = coins[i].target_angle;
                    coins[i].is_flipping = false;
                    coins[i].is_stopping = false;
                    if (!played_confirm_sound) { SYS_SOUND_CONFIRM(); played_confirm_sound = true; }
                }

                int current_half_turn = (int)(coins[i].current_angle / PI);
                if (current_half_turn != coins[i].last_half_turn) {
                    if (!played_nav_sound) { SYS_SOUND_NAV(); played_nav_sound = true; }
                    coins[i].last_half_turn = current_half_turn;
                }
            }

            drawUI();
            
            if (!any_still_flipping) {
                global_is_flipping = false; 
            }
        }
    }

    void onDestroy() override {
        if (img_heads) { free(img_heads); img_heads = nullptr; }
        if (img_tails) { free(img_tails); img_tails = nullptr; }
        if (coin_buffer) { free(coin_buffer); coin_buffer = nullptr; }
    }

    void onKnob(int delta) override {}

    void onKeyShort() override {
        if (global_is_flipping) {
            if (sysConfig.coin_data.mode == 1) {
                // 【应用手动模式刹车圈数】
                int base_spins = CoinAnimParams::MANUAL_STOP_SPINS;
                for (int i = 0; i < active_coins; i++) {
                    if (coins[i].is_flipping && !coins[i].is_stopping) {
                        SYS_SOUND_CONFIRM();
                        coins[i].is_stopping = true;
                        rollCoinWithSanity(i, base_spins);
                        
                        // 【应用错落感圈数】
                        base_spins += CoinAnimParams::EXTRA_SPINS_PER_COIN; 
                        break; 
                    }
                }
            }
            return;
        }

        SYS_SOUND_CONFIRM();
        global_is_flipping = true;
        
        // 【应用自动模式起步圈数】
        int base_spins = CoinAnimParams::AUTO_BASE_SPINS;
        
        for (int i = 0; i < active_coins; i++) {
            coins[i].is_flipping = true;
            coins[i].is_stopping = false;
            coins[i].last_half_turn = (int)(coins[i].current_angle / PI);
            
            // 【应用阻尼系数重置】
            coins[i].step_speed = CoinAnimParams::BASE_DAMPING - (i * CoinAnimParams::DAMPING_DIFF); 

            if (sysConfig.coin_data.mode == 0) {
                rollCoinWithSanity(i, base_spins);
                
                // 【应用错落感圈数】
                base_spins += CoinAnimParams::EXTRA_SPINS_PER_COIN;
            }
        }
        last_frame_time = millis();
    }

    void onKeyLong() override {
        if (!global_is_flipping) {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

AppCoinAnim instanceCoinAnim;

// ==========================================
// 【终端模块设置界面】
// ==========================================
class AppCoinSettings : public AppMenuBase {
private:
    bool is_editing = false; 

protected:
    int getMenuCount() override { return 3; } 

    const char *getTitle() override {
        return appManager.getLanguage() == LANG_ZH ? ">> 决策参数设置" : ">> FLIP SETTINGS";
    }

    const char *getItemText(int index) override {
        static char text_buf[64];
        bool zh = appManager.getLanguage() == LANG_ZH;
        
        const char* cursor = (index == current_selection) ? "> " : "  ";

        if (index == 0) {
            sprintf(text_buf, zh ? "%s运行模式 [ %s ]" : "%sMODE: [ %s ]", cursor, sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL"));
        }
        else if (index == 1) {
            sprintf(text_buf, zh ? "%s理智波动 [ %+d ]" : "%sSANITY: [ %+d ]", cursor, sysConfig.coin_data.sanity);
        }
        else if (index == 2) {
            sprintf(text_buf, zh ? "%s阵列数量 [ %d ]" : "%sCOINS: [ %d ]", cursor, sysConfig.coin_data.coin_count);
        }
        return text_buf;
    }

    bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix) override {
        if (!is_editing || index != current_selection) return false;
        
        static char val_str[16];
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0) {
            *prefix = zh ? ">> 运行模式 [ " : ">> MODE: [ "; 
            *anim_val = sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL");
            *suffix = " ]";
            return true;
        }
        else if (index == 1) {
            *prefix = zh ? ">> 理智波动 [ " : ">> SANITY: [ ";
            sprintf(val_str, "%+d", sysConfig.coin_data.sanity);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        else if (index == 2) {
            *prefix = zh ? ">> 阵列数量 [ " : ">> COINS: [ ";
            sprintf(val_str, "%d", sysConfig.coin_data.coin_count);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        return false; 
    }

    uint16_t getItemColor(int index) override {
        if (index == current_selection) {
            if (is_editing) return 0x1C9F; 
            return 0xFFFF;                 
        }
        return 0x07FF;                     
    }

    void onItemClicked(int index) override {
        SYS_SOUND_CONFIRM();
        is_editing = !is_editing;
        
        if (!is_editing) sysConfig.save(); 
        
        AppMenuBase::onResume(); 
    }

    void onKnob(int delta) override {
        if (is_editing) {
            triggerEditAnimation(delta);

            if (current_selection == 0) {
                sysConfig.coin_data.mode = sysConfig.coin_data.mode == 0 ? 1 : 0; 
            }
            else if (current_selection == 1) {
                sysConfig.coin_data.sanity += delta;
                if (sysConfig.coin_data.sanity > 45) sysConfig.coin_data.sanity = 45;
                if (sysConfig.coin_data.sanity < -45) sysConfig.coin_data.sanity = -45;
            }
            else if (current_selection == 2) {
                sysConfig.coin_data.coin_count += delta;
                if (sysConfig.coin_data.coin_count > 4) sysConfig.coin_data.coin_count = 4;
                if (sysConfig.coin_data.coin_count < 1) sysConfig.coin_data.coin_count = 1;
            }
        }
        else AppMenuBase::onKnob(delta); 
    }

    void onLongPressed() override {
        if (is_editing) { 
            is_editing = false; 
            SYS_SOUND_CONFIRM(); 
            sysConfig.save(); 
            AppMenuBase::onResume(); 
        }
        else { 
            SYS_SOUND_NAV(); 
            appManager.popApp(); 
        }
    }
};
AppCoinSettings instanceCoinSettings;

// ==========================================
// 【系统主入口】
// ==========================================
class AppCoinMenu : public AppMenuBase {
protected:
    int getMenuCount() override { return 2; }

    const char *getTitle() override {
        return appManager.getLanguage() == LANG_ZH ? ">> 量子决策模块" : ">> QUANTUM FLIP";
    }

    const char *getItemText(int index) override {
        bool zh = appManager.getLanguage() == LANG_ZH;
        switch (index) {
        case 0: return zh ? "开始推演协议" : "START PROTOCOL";
        case 1: return zh ? "终端模块设置" : "MODULE SETTINGS";
        }
        return "";
    }

    void onItemClicked(int index) override {
        if (index == 0) appManager.pushApp(&instanceCoinAnim);
        else if (index == 1) appManager.pushApp(&instanceCoinSettings);
    }

    void onLongPressed() override { SYS_SOUND_NAV(); appManager.popApp(); }
};

AppCoinMenu instanceCoinMenu;
AppBase *appCoinFlip = &instanceCoinMenu;