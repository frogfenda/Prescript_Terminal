// 文件：src/apps/app_coin_flip.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>
#include "sys/sys_config.h"
#include "sys/sys_audio.h"
#include "sys/sys_res.h"
#include "sys_haptic.h"
#include "sys/sys_event.h" 
#include "sys/sys_ble.h"   

int g_coin_run_idx = -1;  // 告诉动画引擎当前在跑哪个技能 (-1代表快速推演)
int g_coin_edit_idx = -1; // 告诉编辑界面当前在改哪个技能
extern AppBase* appCoinPresetEdit; 
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

// ==========================================
// 【系统总线】：全自动监听蓝牙下发的预设指令
// ==========================================
class CoinEventReceiver {
public:
    CoinEventReceiver() {
        SysEvent_Subscribe(EVT_COIN_PRESET_ADD, onCoinAdd);
        SysEvent_Subscribe(EVT_COIN_PRESET_DEL, onCoinDel);
        SysEvent_Subscribe(EVT_BLE_SYNC_REQ, onSyncReq); 
    }
    
    static void onSyncReq(void* payload) {
        for (int i = 0; i < sysConfig.coin_preset_count; i++) {
            CoinPreset& p = sysConfig.coin_presets[i];
            char buf[256];
            sprintf(buf, "SYNC:COIN:{\"n\":\"%s\",\"bp\":%d,\"cp\":%d,\"cc\":%d,\"cl\":\"%s\"}",
                    p.name.c_str(), p.base_power, p.coin_power, p.coin_count, p.coin_colors.c_str());
            SysBLE_Notify(buf);
            vTaskDelay(pdMS_TO_TICKS(20)); 
        }
    }

    static void onCoinAdd(void* payload) {
        Evt_CoinAdd_t* p = (Evt_CoinAdd_t*)payload;
        String name = String(p->name);
        int idx = -1;
        
        for(int i=0; i<sysConfig.coin_preset_count; i++) {
            if (sysConfig.coin_presets[i].name == name) { idx = i; break; }
        }
        if (idx == -1 && sysConfig.coin_preset_count < 10) {
            idx = sysConfig.coin_preset_count;
            sysConfig.coin_preset_count++;
        }
        
        if (idx != -1) {
            sysConfig.coin_presets[idx].name = name;
            sysConfig.coin_presets[idx].base_power = p->bp;
            sysConfig.coin_presets[idx].coin_power = p->cp;
            sysConfig.coin_presets[idx].coin_count = p->cc;
            sysConfig.coin_presets[idx].coin_colors = String(p->colors); 
            sysConfig.save(); 
            SysBLE_Notify("SYNC:CLEAR"); 
        }
    }

    static void onCoinDel(void* payload) {
        Evt_CoinDel_t* p = (Evt_CoinDel_t*)payload;
        String name = String(p->name);
        int idx = -1;
        
        for(int i=0; i<sysConfig.coin_preset_count; i++) {
            if (sysConfig.coin_presets[i].name == name) { idx = i; break; }
        }
        
        if (idx != -1) {
            for (int i=idx; i<sysConfig.coin_preset_count-1; i++) {
                sysConfig.coin_presets[i] = sysConfig.coin_presets[i+1];
            }
            sysConfig.coin_preset_count--;
            sysConfig.save();
            SysBLE_Notify("SYNC:CLEAR");
        }
    }
};

CoinEventReceiver g_coinEventReceiver;

// ==========================================
// 核心物理引擎 (基类)：解锁 48KB 显存，多通道材质并发加载
// ==========================================
const int REAL_SW = 284; 
const int REAL_SH = 76;  

class AppCoinCore : public AppBase {
protected:
    uint16_t *img_heads[3] = {nullptr, nullptr, nullptr};
    uint16_t *img_tails[3] = {nullptr, nullptr, nullptr};
    uint16_t *coin_buffer = nullptr;

    CoinEntity coins[9]; 
    int active_coins = 1;
    int current_coin_size = SRC_COIN_SIZE;

    bool global_is_animating = false;
    uint32_t last_frame_time = 0;

    virtual int getLeftPanelWidth() { return 0; }
    virtual int getTopPanelHeight() { return 0; } 
    virtual void drawStaticUI() {}
    virtual bool drawDynamicUI() { return false; }
    virtual void onCoinStop(int idx) {}
    virtual void onAllCoinsStopped() {}

    void loadAllCoinData() {
        if (!coin_buffer) coin_buffer = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
        String prefixes[3] = {"/assets/coins/", "/assets/coins/r", "/assets/coins/g"};
        for(int i = 0; i < 3; i++) {
            if (!img_heads[i]) img_heads[i] = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
            if (!img_tails[i]) img_tails[i] = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
            
            File f1 = LittleFS.open((prefixes[i] + "heads.bin").c_str(), "r");
            if (!f1) f1 = LittleFS.open("/assets/coins/heads.bin", "r"); 
            if (f1) { f1.read((uint8_t *)img_heads[i], SRC_COIN_SIZE * SRC_COIN_SIZE * 2); f1.close(); }

            File f2 = LittleFS.open((prefixes[i] + "tails.bin").c_str(), "r");
            if (!f2) f2 = LittleFS.open("/assets/coins/tails.bin", "r");
            if (f2) { f2.read((uint8_t *)img_tails[i], SRC_COIN_SIZE * SRC_COIN_SIZE * 2); f2.close(); }
        }
    }

    void eraseRect(int x, int y, int w, int h) {
        if (w * h > SRC_COIN_SIZE * SRC_COIN_SIZE) return; 
        memset(coin_buffer, 0, w * h * 2);
        HAL_Sprite_PushImage(x, y, w, h, coin_buffer);
    }

    void drawScaledCoinToBuffer(int idx, float scaleX, int target_size) {
        memset(coin_buffer, 0, target_size * target_size * 2);
        
        int c_type = 0;
        if (g_coin_run_idx >= 0) {
            String cl_str = sysConfig.coin_presets[g_coin_run_idx].coin_colors;
            if (cl_str.length() > 0) {
                int char_idx = (idx < cl_str.length()) ? idx : (cl_str.length() - 1);
                c_type = cl_str[char_idx] - '0';
            }
        } else {
            c_type = sysConfig.coin_data.coin_type;
        }
        if (c_type < 0 || c_type > 2) c_type = 0;

        uint16_t *img = (scaleX >= 0) ? img_heads[c_type] : img_tails[c_type];
        if (!img) return;

        bool is_spinning = coins[idx].is_flipping;
        bool is_flashing = (coins[idx].flash_frames > 0);
        bool is_dimmed = (!is_spinning && coins[idx].target_face == 1);

        int drawW = target_size * fabs(scaleX) * CoinAnimParams::PERSPECTIVE_SCALE;
        if (drawW % 2 != 0) drawW++;
        if (drawW <= 1) return;

        int startX = (target_size - drawW) / 2;
        bool is_back = (scaleX < 0);

        uint8_t x_map[SRC_COIN_SIZE];
        for (int x = 0; x < drawW; x++) {
            int origX = x * (SRC_COIN_SIZE - 1) / (drawW - 1);
            if (is_back) origX = (SRC_COIN_SIZE - 1) - origX;
            x_map[x] = origX;
        }

        for (int dst_y = 0; dst_y < target_size; dst_y++) {
            int src_y = dst_y * SRC_COIN_SIZE / target_size;
            if (src_y >= SRC_COIN_SIZE) src_y = SRC_COIN_SIZE - 1;
            int src_y_offset = src_y * SRC_COIN_SIZE;
            int dst_y_offset = dst_y * target_size;

            for (int dst_x = 0; dst_x < drawW; dst_x++) {
                uint16_t color = img[src_y_offset + x_map[dst_x]];
                if (color != 0x0000) {
                    if (is_flashing && !is_back && !is_spinning) {
                        uint32_t intensity = (coins[idx].flash_frames * 256) / CoinAnimParams::FLASH_DURATION;
                        uint16_t r = (color >> 11) & 0x1F; uint16_t g = (color >> 5) & 0x3F; uint16_t b = color & 0x1F;
                        r = r + (((31 - r) * intensity) >> 8); g = g + (((63 - g) * intensity) >> 8); b = b + (((31 - b) * intensity) >> 8);
                        color = (r << 11) | (g << 5) | b;
                    }
                    coin_buffer[dst_y_offset + startX + dst_x] = color;
                }
            }
        }
    }

    bool drawActiveCoinsOnly() {
        int left_w = getLeftPanelWidth();
        int top_h = getTopPanelHeight(); 
        
        int draw_sw = REAL_SW - left_w;
        int draw_sh = REAL_SH - top_h;   
        bool screen_needs_push = false; 

        int min_gap = 4;
        int max_w = (draw_sw - (active_coins + 1) * min_gap) / active_coins;
        int max_h = draw_sh - 4; 
        
        current_coin_size = (max_w < max_h) ? max_w : max_h;
        if (current_coin_size > SRC_COIN_SIZE) current_coin_size = SRC_COIN_SIZE;
        
        int actual_gap_x = (draw_sw - (active_coins * current_coin_size)) / (active_coins + 1);
        int target_y = top_h + (draw_sh - current_coin_size) / 2; 

        for (int i = 0; i < active_coins; i++) {
            if (coins[i].needs_redraw || coins[i].is_flipping || coins[i].flash_frames > 0) {
                float scale = cos(coins[i].current_angle);
                drawScaledCoinToBuffer(i, scale, current_coin_size);
                
                int target_x = left_w + actual_gap_x + i * (current_coin_size + actual_gap_x);
                HAL_Sprite_PushImage(target_x, target_y, current_coin_size, current_coin_size, coin_buffer);
                
                screen_needs_push = true; 
                coins[i].needs_redraw = false;
            }
        }
        return screen_needs_push;
    }

    void stopCoin(int idx) {
        coins[idx].is_flipping = false;
        int heads_chance = 50 + sysConfig.coin_data.sanity;
        coins[idx].target_face = (random(100) < heads_chance) ? 0 : 1;
        coins[idx].current_angle = (coins[idx].target_face == 0) ? 0.0f : PI;
        coins[idx].needs_redraw = true;
        onCoinStop(idx);
    }

public:
    void onLoop() override {
        uint32_t now = millis();
        if (now - last_frame_time < CoinAnimParams::FRAME_DELAY_MS) return;
        last_frame_time = now;

        bool any_active = false;
        if (global_is_animating) {
            for (int i = 0; i < active_coins; i++) {
                if (coins[i].is_flipping) {
                    any_active = true;
                    coins[i].current_angle += CoinAnimParams::RAPID_SPIN_STEP;
                    if (sysConfig.coin_data.mode == 0) {
                        if (coins[i].auto_stop_timer > 0) coins[i].auto_stop_timer--;
                        else stopCoin(i);
                    }
                }
                if (coins[i].flash_frames > 0) {
                    any_active = true;
                    coins[i].flash_frames--;
                    if (coins[i].flash_frames == 0) coins[i].needs_redraw = true;
                }
            }
        }

        bool ui_pushed = drawDynamicUI();
        bool coin_pushed = drawActiveCoinsOnly();
        if (ui_pushed || coin_pushed) HAL_Screen_Update();

        if (global_is_animating && !any_active) {
            global_is_animating = false;
            onAllCoinsStopped();
        }
    }

    void onDestroy() override {
        sysAudio.stopWAV();
        for(int i=0; i<3; i++) {
            if (img_heads[i]) { free(img_heads[i]); img_heads[i] = nullptr; }
            if (img_tails[i]) { free(img_tails[i]); img_tails[i] = nullptr; }
        }
        if (coin_buffer) { free(coin_buffer); coin_buffer = nullptr; }
    }
};

// ==========================================
// 派生 A：经典推演模式
// ==========================================
class AppCoinQuick : public AppCoinCore {
protected:
    int getLeftPanelWidth() override { return 0; } 
    int getTopPanelHeight() override { return 0; } 

    void drawStaticUI() override {
        if (sysConfig.coin_data.sanity != 0) {
            char san_str[16];
            sprintf(san_str, "%+d", sysConfig.coin_data.sanity);
            uint16_t color = (sysConfig.coin_data.sanity < 0) ? 0xF800 : 0x07FF;
            HAL_Screen_ShowChineseLine_Faded_Color(REAL_SW - HAL_Get_Text_Width(san_str) - 4, REAL_SH - 18, san_str, 0.0f, color);
        }
    }

    void onCoinStop(int idx) override {
        sysAudio.stopWAV();
        if (coins[idx].target_face == 0) {
            coins[idx].flash_frames = CoinAnimParams::FLASH_DURATION;
            if (g_wav_heads) sysAudio.playWAV(g_wav_heads, g_wav_heads_len);
            else SYS_SOUND_CONFIRM();
            SYS_HAPTIC_COIN_HEADS();
        } else {
            if (g_wav_tails) sysAudio.playWAV(g_wav_tails, g_wav_tails_len);
            else SYS_SOUND_NAV();
            SYS_HAPTIC_COIN_TAILS();
        }
    }

public:
    void onCreate() override {
        loadAllCoinData(); 
        active_coins = sysConfig.coin_data.coin_count;
        if (active_coins < 1) active_coins = 1; if (active_coins > 9) active_coins = 9;
        global_is_animating = false;
        for(int i=0; i<active_coins; i++) { coins[i].current_angle = 0; coins[i].is_flipping = false; coins[i].flash_frames = 0; coins[i].needs_redraw = true; coins[i].target_face = 0; }
        HAL_Sprite_Clear(); drawActiveCoinsOnly(); drawStaticUI(); HAL_Screen_Update();
    }
    void onResume() override { onCreate(); }
    void onKnob(int delta) override {}

    void onKeyShort() override {
        if (global_is_animating) {
            if (sysConfig.coin_data.mode == 1) { for (int i=0; i<active_coins; i++) if (coins[i].is_flipping) { stopCoin(i); break; } }
            return;
        }
        sysAudio.stopWAV(); SYS_SOUND_CONFIRM(); global_is_animating = true;
        for (int i=0; i<active_coins; i++) {
            coins[i].is_flipping = true; coins[i].flash_frames = 0; coins[i].target_face = 0;
            if (sysConfig.coin_data.mode == 0) coins[i].auto_stop_timer = CoinAnimParams::AUTO_SPIN_INITIAL + i * CoinAnimParams::AUTO_SPIN_INTERVAL;
        }
        last_frame_time = millis();
    }
    void onKeyLong() override { SYS_SOUND_NAV(); appManager.popApp(); }
};

// ==========================================
// 派生 B：硬核技能拼点模式
// ==========================================
class AppCoinSkill : public AppCoinCore {
private:
    int current_power = 0;
    int phase = 0; 
    uint32_t blink_last_time = 0;
    
    int top_pop_timer = 0;
    String top_pop_text = "";
    uint16_t top_pop_color = TFT_WHITE;

protected:
    int getLeftPanelWidth() override { return 0; } 
    int getTopPanelHeight() override { return 26; } 

    void drawStaticUI() override {
        HAL_Draw_Line(0, 25, REAL_SW, 25, 0x18E3);
        eraseRect(0, 0, 140, 24); 
        String name = sysConfig.coin_presets[g_coin_run_idx].name;
        HAL_Screen_ShowChineseLine_Faded_Color(6, 6, name.c_str(), 0.0f, TFT_CYAN);
        updateTopPanelScore(TFT_WHITE);
    }

    void updateTopPanelScore(uint16_t color) {
        eraseRect(140, 0, 144, 24); 
        char pow_str[16]; sprintf(pow_str, "%d", current_power); 
        int score_w = HAL_Get_Text_Width(pow_str);
        int score_x = REAL_SW - score_w - 8; int score_y = 6; 
        HAL_Screen_ShowChineseLine_Faded_Color(score_x, score_y, pow_str, 0.0f, color);

        if (top_pop_timer > 0) {
            int pop_w = HAL_Get_Text_Width(top_pop_text.c_str());
            int pop_x = score_x - pop_w - 6; if (pop_x < 140) pop_x = 140;    
            int float_up = (20 - top_pop_timer) * 6 / 20; int pop_y = score_y - float_up; 
            float fade_alpha = (20.0f - top_pop_timer) / 20.0f;
            HAL_Screen_ShowChineseLine_Faded_Color(pop_x, pop_y, top_pop_text.c_str(), fade_alpha, top_pop_color);
        }
    }

    bool drawDynamicUI() override {
        bool needs_push = false;
        if (top_pop_timer > 0) { top_pop_timer--; needs_push = true; }
        if (phase == 2) {
            uint32_t now = millis();
            if (now - blink_last_time > 100) { blink_last_time = now; needs_push = true; }
        }
        if (needs_push) {
            uint16_t num_color = (phase == 2 && (millis()/100)%2 == 0) ? TFT_RED : TFT_WHITE;
            updateTopPanelScore(num_color);
        }
        return needs_push;
    }

    void onCoinStop(int idx) override {
        sysAudio.stopWAV();
        CoinPreset& p = sysConfig.coin_presets[g_coin_run_idx];

        if (coins[idx].target_face == 0) {
            current_power += p.coin_power;
            coins[idx].flash_frames = CoinAnimParams::FLASH_DURATION;
            top_pop_text = (p.coin_power >= 0 ? "+" : "") + String(p.coin_power);
            top_pop_color = TFT_YELLOW;
            top_pop_timer = 20; 
            updateTopPanelScore(TFT_WHITE); 
            if (g_wav_heads) sysAudio.playWAV(g_wav_heads, g_wav_heads_len); else SYS_SOUND_CONFIRM();
            SYS_HAPTIC_COIN_HEADS();
        } else {
            top_pop_text = "+0";
            top_pop_color = 0x7BEF;
            top_pop_timer = 20;
            updateTopPanelScore(TFT_WHITE); 
            if (g_wav_tails) sysAudio.playWAV(g_wav_tails, g_wav_tails_len); else SYS_SOUND_NAV();
            SYS_HAPTIC_COIN_TAILS();
        }
    }
    void onAllCoinsStopped() override { phase = 2; }

public:
    void onCreate() override {
        CoinPreset& p = sysConfig.coin_presets[g_coin_run_idx];
        loadAllCoinData(); 
        active_coins = p.coin_count; current_power = p.base_power;
        if (active_coins < 1) active_coins = 1; if (active_coins > 9) active_coins = 9; 
        
        global_is_animating = false; phase = 0; top_pop_timer = 0;
        for(int i=0; i<active_coins; i++) { coins[i].current_angle = 0; coins[i].is_flipping = false; coins[i].flash_frames = 0; coins[i].needs_redraw = true; coins[i].target_face = 0; }
        HAL_Sprite_Clear(); drawActiveCoinsOnly(); drawStaticUI(); HAL_Screen_Update();
    }
    
    void onResume() override { onCreate(); }
    void onKnob(int delta) override {}

    void onKeyShort() override {
        if (global_is_animating) {
            if (sysConfig.coin_data.mode == 1) { for (int i=0; i<active_coins; i++) if (coins[i].is_flipping) { stopCoin(i); break; } }
            return;
        }
        if (phase == 2) {
            phase = 0; current_power = sysConfig.coin_presets[g_coin_run_idx].base_power; 
            HAL_Sprite_Clear(); for (int i=0; i<active_coins; i++) coins[i].needs_redraw = true;
            drawActiveCoinsOnly(); drawStaticUI(); HAL_Screen_Update();
            return;
        }
        sysAudio.stopWAV(); SYS_SOUND_CONFIRM();
        global_is_animating = true; phase = 1; current_power = sysConfig.coin_presets[g_coin_run_idx].base_power; top_pop_timer = 0;
        for (int i=0; i<active_coins; i++) {
            coins[i].is_flipping = true; coins[i].flash_frames = 0; coins[i].target_face = 0;
            if (sysConfig.coin_data.mode == 0) coins[i].auto_stop_timer = CoinAnimParams::AUTO_SPIN_INITIAL + i * CoinAnimParams::AUTO_SPIN_INTERVAL;
        }
        last_frame_time = millis();
    }
    void onKeyLong() override { SYS_SOUND_NAV(); appManager.popApp(); }
};

AppCoinQuick instanceCoinQuick;
AppBase *appCoinQuick = &instanceCoinQuick;

AppCoinSkill instanceCoinSkill;
AppBase *appCoinSkill = &instanceCoinSkill;

// ==========================================
// 【终端模块设置界面】
// ==========================================
class AppCoinSettings : public AppMenuBase
{
private:
    bool is_editing = false;

protected:
    int getMenuCount() override { return 4; } 

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
        { 
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
            { 
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
// 【终端本地操作】：量子技能录入与编辑终端
// ==========================================
class AppCoinPresetEdit : public AppBase {
private:
    int bp, cp, cc, cl, phase;
    DialAnimator dialAnim;       
    TacticalLinkEngine linkAnim; 

    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 4) {
            const char *title = zh ? "战术覆写" : "OVERRIDE";
            HAL_Screen_ShowChineseLine(10, 26, title);

            char buf[64];
            if (g_coin_edit_idx >= 0) {
                sprintf(buf, zh ? "覆写 [%s]?" : "OVERRIDE [%s]?", sysConfig.coin_presets[g_coin_edit_idx].name.c_str());
            } else {
                sprintf(buf, zh ? "录入为新技能?" : "SAVE AS NEW?");
            }
            int txt_w = HAL_Get_Text_Width(buf);
            HAL_Screen_ShowChineseLine(sw - txt_w - 10, 26, buf);

            const char *tip = zh ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: CONFIRM";
            if (g_coin_edit_idx >= 0) tip = zh ? "长按抹除 / 单击确认" : "LONG: DELETE / CLICK: CONFIRM";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        } else {
            const char *names_zh[] = {"基础点", "硬币点", "抛掷数", "材质"};
            const char *names_en[] = {"BASE", "COIN", "COUNT", "MAT"};
            const char **names = zh ? names_zh : names_en;

            linkAnim.draw(2, names, 4, phase, 58);

            int line_y = 18;
            HAL_Draw_Line(0, line_y, sw / 2 - 30, line_y, 1);
            HAL_Draw_Line(sw / 2 - 30, line_y, sw / 2 - 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 - 25, line_y + 3, sw / 2 + 25, line_y + 3, 1);
            HAL_Draw_Line(sw / 2 + 25, line_y + 3, sw / 2 + 30, line_y, 1);
            HAL_Draw_Line(sw / 2 + 30, line_y, sw, line_y, 1);

            if (phase == 0) dialAnim.drawNumberDial(28, bp, 0, 99, "");
            else if (phase == 1) dialAnim.drawNumberDial(28, cp, -20, 99, "");
            else if (phase == 2) dialAnim.drawNumberDial(28, cc, 1, 9, "");
            else if (phase == 3) {
                const char *c_zh[] = {"经典金", "狂气红", "沉稳绿"};
                const char *c_en[] = {"GOLD", "RED", "GREEN"};
                dialAnim.drawStringDial(28, cl, zh ? c_zh : c_en, 3); 
            }

            const char *tip = zh ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
            if (phase == 0 && g_coin_edit_idx < 0) tip = zh ? "长按取消 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
            HAL_Screen_ShowChineseLine_Faded((sw - HAL_Get_Text_Width(tip)) / 2, 56, tip, 0.6f);
        }
        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        if (g_coin_edit_idx >= 0) {
            CoinPreset& p = sysConfig.coin_presets[g_coin_edit_idx];
            bp = p.base_power; cp = p.coin_power; cc = p.coin_count;
            cl = (p.coin_colors.length() > 0) ? (p.coin_colors[0] - '0') : 0;
        } else {
            bp = 4; cp = 5; cc = 3; cl = 0; 
        }
        phase = 0; linkAnim.jumpTo(phase); drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if (d_anim || l_anim) drawUI();
    }

    void onDestroy() override {}

    void onKnob(int delta) override {
        if (phase == 4) return;
        if (phase == 0) { bp += delta; if(bp < 0) bp = 99; if(bp > 99) bp = 0; }
        else if (phase == 1) { cp += delta; if(cp < -20) cp = 99; if(cp > 99) cp = -20; }
        else if (phase == 2) { cc += delta; if(cc < 1) cc = 9; if(cc > 9) cc = 1; }
        else if (phase == 3) { cl += delta; if(cl < 0) cl = 2; if(cl > 2) cl = 0; }
        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    void onKeyShort() override {
        SYS_SOUND_CONFIRM();
        if (phase < 4) {
            phase++;
            drawUI();
        } else {
            if (g_coin_edit_idx >= 0) {
                int idx = g_coin_edit_idx;
                sysConfig.coin_presets[idx].base_power = bp;
                sysConfig.coin_presets[idx].coin_power = cp;
                sysConfig.coin_presets[idx].coin_count = cc;
                String c_str = ""; for(int i=0; i<cc; i++) c_str += String(cl);
                sysConfig.coin_presets[idx].coin_colors = c_str;
            } else {
                int idx = sysConfig.coin_preset_count;
                sysConfig.coin_presets[idx].base_power = bp;
                sysConfig.coin_presets[idx].coin_power = cp;
                sysConfig.coin_presets[idx].coin_count = cc;
                String c_str = ""; for(int i=0; i<cc; i++) c_str += String(cl);
                sysConfig.coin_presets[idx].coin_colors = c_str;
                char autoName[16];
                sprintf(autoName, "预设-%d", idx + 1);
                sysConfig.coin_presets[idx].name = autoName;
                sysConfig.coin_preset_count++;
            }
            sysConfig.save();
            appManager.popApp();
        }
    }

    void onKeyLong() override {
        if (phase == 4) {
            if (g_coin_edit_idx >= 0) {
                SYS_SOUND_GLITCH();
                for (int i = g_coin_edit_idx; i < sysConfig.coin_preset_count - 1; i++)
                    sysConfig.coin_presets[i] = sysConfig.coin_presets[i + 1];
                sysConfig.coin_preset_count--;
                sysConfig.save();
                appManager.popApp();
            } else {
                SYS_SOUND_NAV();
                phase = 3; 
                drawUI();
            }
        } else if (phase > 0) {
            SYS_SOUND_NAV();
            phase--;
            drawUI();
        } else {
            SYS_SOUND_NAV();
            appManager.popApp(); 
        }
    }
};

AppCoinPresetEdit instanceCoinPresetEdit;
AppBase *appCoinPresetEdit = &instanceCoinPresetEdit;

// ==========================================
// 【系统主入口】：动态硬币技能菜单
// ==========================================
class AppCoinMenu : public AppMenuBase
{
protected:
    int getMenuCount() override { 
        return sysConfig.coin_preset_count + 3; 
    }
    
    const char *getTitle() override { 
        return appManager.getLanguage() == LANG_ZH ? ">> 量子决策模块" : ">> QUANTUM FLIP"; 
    }
    
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        
        if (index == 0) 
            return zh ? "[ 快速基础推演 ]" : "[ QUICK ROLL ]";
            
        if (index == sysConfig.coin_preset_count + 1) 
            return zh ? " + 新建技能预设" : " + NEW PRESET";
            
        if (index == sysConfig.coin_preset_count + 2) 
            return zh ? "模块高级设定" : "ADVANCED SETTINGS";

        static char buf[64];
        CoinPreset &p = sysConfig.coin_presets[index - 1];
        sprintf(buf, "[%s]  %d + %dx%d", p.name.c_str(), p.base_power, p.coin_power, p.coin_count);
        return buf;
    }

   void onItemClicked(int index) override
    {
        if (index == 0) {
            g_coin_run_idx = -1;
            appManager.pushApp(appCoinQuick); 
        } 
        else if (index == sysConfig.coin_preset_count + 1) {
            if (sysConfig.coin_preset_count < 10) {
                g_coin_edit_idx = -1; 
                appManager.pushApp(appCoinPresetEdit);
            }
        } 
        else if (index == sysConfig.coin_preset_count + 2) {
            appManager.pushApp(&instanceCoinSettings);
        } 
        else {
            g_coin_run_idx = index - 1; 
            appManager.pushApp(appCoinSkill); 
        }
    }

    void onLongPressed() override
    {
        if (current_selection > 0 && current_selection <= sysConfig.coin_preset_count) {
            g_coin_edit_idx = current_selection - 1;
            appManager.pushApp(appCoinPresetEdit); 
        } else {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

AppCoinMenu instanceCoinMenu;
AppBase *appCoinFlip = &instanceCoinMenu;