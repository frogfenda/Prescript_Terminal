/*
【模块职责】硬币系统。包含快速投掷、技能预设投掷、硬币参数设置和预设编辑；动画用硬币贴图横向压缩模拟翻面。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_coin_flip.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include "../ui/ui_frame.h"
#include <LittleFS.h>
#include "sys/sys_config.h"
#include "sys/sys_audio.h"
#include "sys/sys_res.h"
#include "sys_haptic.h"
#include "sys/sys_event.h" 
#include "sys/sys_ble.h"   
#include "sys/sys_constants.h"
#include "sys/sys_command_result.h"
#include "../ui/ui_coin.h"

int g_coin_run_idx = -1;  // 告诉动画引擎当前在跑哪个技能 (-1代表快速推演)
int g_coin_edit_idx = -1; // 告诉编辑界面当前在改哪个技能
// 硬币运行时绘制缓冲区跟随 ui_coin 的建议最大显示尺寸。
// 贴图源尺寸由资源管家根据 bin 文件自动识别，当前兼容 64×64 与 96×96。
const int SRC_COIN_SIZE = UICoin::COIN_RENDER_BUFFER_SIZE;

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
    
    // 【函数说明】WebBLE 同步回调：把当前硬币预设数组逐条打包成 SYNC:COIN JSON 回传网页。
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

    // 【函数说明】处理 COIN 命令：按技能名查找现有预设，存在则更新，不存在则追加，并保存配置和写 ACK。
    static void onCoinAdd(void* payload) {
        Evt_CoinAdd_t* p = (Evt_CoinAdd_t*)payload;
        String name = String(p->name);
        name.trim();
        if (name.length() == 0)
        {
            SysCmdResult_Error("EMPTY_NAME");
            return;
        }
        if (p->cc < 1 || p->cc > PrescriptConst::MAX_COIN_COUNT)
        {
            SysCmdResult_Error("INVALID_COIN_COUNT");
            return;
        }

        int idx = -1;
        for(int i=0; i<sysConfig.coin_preset_count; i++) {
            if (sysConfig.coin_presets[i].name == name) { idx = i; break; }
        }

        bool updated = (idx >= 0);
        if (idx == -1) {
            if (sysConfig.coin_preset_count >= PrescriptConst::MAX_COIN_PRESETS)
            {
                SysCmdResult_Error("FULL");
                return;
            }
            idx = sysConfig.coin_preset_count;
            sysConfig.coin_preset_count++;
        }

        sysConfig.coin_presets[idx].name = name;
        sysConfig.coin_presets[idx].base_power = p->bp;
        sysConfig.coin_presets[idx].coin_power = p->cp;
        sysConfig.coin_presets[idx].coin_count = p->cc;
        sysConfig.coin_presets[idx].coin_colors = String(p->colors);
        sysConfig.save();
        SysBLE_Notify("SYNC:CLEAR");

        if (updated)
            SysCmdResult_Warn("UPDATED", name);
        else
            SysCmdResult_Ok("ADDED", name);
    }

    // 【函数说明】处理 COIN_DEL 命令：按技能名删除预设，移动后续元素填补空位，保存配置并返回 DELETED/NOT_FOUND。
    static void onCoinDel(void* payload) {
        Evt_CoinDel_t* p = (Evt_CoinDel_t*)payload;
        String name = String(p->name);
        name.trim();
        if (name.length() == 0)
        {
            SysCmdResult_Error("EMPTY_NAME");
            return;
        }

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
            SysCmdResult_Ok("DELETED", name);
        }
        else
        {
            SysCmdResult_Error("NOT_FOUND", name);
        }
    }
};

CoinEventReceiver g_coinEventReceiver;

// ==========================================
// 核心物理引擎 (基类)：解锁 48KB 显存，多通道材质并发加载
// ==========================================
constexpr int REAL_SW = PrescriptConst::UI_SCREEN_WIDTH;
constexpr int REAL_SH = PrescriptConst::UI_SCREEN_HEIGHT;

class AppCoinCore : public AppBase {
protected:
    uint16_t *coin_buffer = nullptr;

    CoinEntity coins[UICoin::MAX_COINS]; 
    int active_coins = 1;
    int current_coin_size = SRC_COIN_SIZE;

    bool global_is_animating = false;
    uint32_t last_frame_time = 0;

    // 【函数说明】返回硬币动画左侧保留区域宽度；快速和技能投掷都使用全宽动画，所以返回 0。
    virtual int getLeftPanelWidth() { return 0; }
    virtual int getTopPanelHeight() { return 0; } 
    virtual void drawStaticUI() {}
    virtual bool drawDynamicUI() { return false; }
    // 【函数说明】绘制需要压在硬币上方的常驻覆盖信息，例如理智波动值。
    // 返回 true 表示当前 Sprite 内容发生变化，需要本帧推屏。
    virtual bool drawOverlayUI() { return false; }
    // 【函数说明】单枚硬币停止时的回调，子类用它更新计数、点数和结果反馈。
    virtual void onCoinStop(int idx) {}
    virtual void onAllCoinsStopped() {}

   
    /**
     * 清除硬币页上的局部区域。
     *
     * 旧屏时代这个函数只用 64×64 coin_buffer 当“黑色橡皮擦”，
     * 因此超过 4096 像素的区域会直接 return。新屏顶部技能面板变宽后，
     * 左右半屏擦除区域会超过 64×64，如果继续 return，会留下旧文字残影。
     *
     * 处理策略：
     * - 小区域继续用黑色缓冲区 + PushImage，避免过度依赖 fillRect；
     * - 大区域直接在 Sprite 上填黑，等本帧 HAL_Screen_Update() 统一推屏。
     */
    void eraseRect(int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return;

        if (coin_buffer && w * h <= SRC_COIN_SIZE * SRC_COIN_SIZE) {
            memset(coin_buffer, 0, w * h * 2);
            HAL_Sprite_PushImage(x, y, w, h, coin_buffer);
            return;
        }

        HAL_Fill_Rect(x, y, w, h, TFT_BLACK);
    }

    // 【函数说明】把理智波动值画成小型覆盖徽标，统一用于快速模式和技能预设模式。
    // 这个徽标每次都在硬币重绘之后补画，避免大硬币刷新时把右上角理智值盖掉。
    bool drawSanityBadgeAt(int x, int y)
    {
        if (sysConfig.coin_data.sanity == 0)
            return false;

        char san_str[16];
        sprintf(san_str, "%+d", sysConfig.coin_data.sanity);
        int text_w = HAL_Get_Text_Width_Font(san_str, HAL_FONT_BODY);
        int text_h = HAL_Get_Font_Line_Height(HAL_FONT_BODY);
        int box_w = text_w + 10;
        int box_h = text_h + 6;
        int box_x = x - box_w;
        int box_y = y;
        if (box_x < 0) box_x = 0;
        if (box_y < 0) box_y = 0;
        if (box_x + box_w > REAL_SW) box_x = REAL_SW - box_w;
        if (box_y + box_h > REAL_SH) box_y = REAL_SH - box_h;

        uint16_t color = (sysConfig.coin_data.sanity < 0) ? 0xF800 : 0x07FF;
        HAL_Fill_Rect(box_x, box_y, box_w, box_h, TFT_BLACK);
        HAL_Draw_Rect(box_x, box_y, box_w, box_h, color);
        HAL_Screen_ShowChineseLine_Faded_Color(box_x + 5, box_y + 3, san_str, 0.0f, color);
        return true;
    }

    // 【函数说明】按当前硬币状态把一枚硬币渲染到 coin_buffer。实际贴图缩放和高亮逻辑下沉到 ui_coin。
    void drawScaledCoinToBuffer(int idx, float scaleX, int target_size) {
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

        UICoin::CoinFrame frame;
        frame.scaleX = scaleX;
        frame.isFlipping = coins[idx].is_flipping;
        frame.targetFace = coins[idx].target_face;
        frame.flashFrames = coins[idx].flash_frames;
        frame.flashDuration = CoinAnimParams::FLASH_DURATION;
        frame.material = c_type;
        UICoin::DrawCoinToBuffer(coin_buffer, SRC_COIN_SIZE, frame, target_size);
    }

    /**
     * 只重绘仍在翻转或需要闪烁的硬币。
     *
     * 动画效果：
     * - 每枚硬币仍然使用 64×64 RGB565 原始素材；
     * - 根据当前角度 cos(angle) 计算横向压缩比例；
     * - 压缩到接近 0 时形成硬币侧面翻转效果；
     * - 停在正面时会有短暂白色闪光。
     *
     * 大屏适配点：
     * - 不再使用旧屏固定 4px 间距；
     * - 根据当前可绘制区域宽高动态计算硬币尺寸和间距；
     * - 仍然限制最大尺寸为 SRC_COIN_SIZE，避免读取 64×64 素材时越界。
     */
    bool drawActiveCoinsOnly() {
        int left_w = getLeftPanelWidth();
        int top_h = getTopPanelHeight(); 
        int draw_sw = REAL_SW - left_w;
        int draw_sh = REAL_SH - top_h;
        bool screen_needs_push = false; 

        UICoin::StageLayout layout = UICoin::BuildStageLayout(
            active_coins,
            left_w,
            top_h,
            draw_sw,
            draw_sh,
            top_h > 0
        );
        current_coin_size = layout.coinSize;

        for (int i = 0; i < active_coins; i++) {
            if (coins[i].needs_redraw || coins[i].is_flipping || coins[i].flash_frames > 0) {
                float scale = cos(coins[i].current_angle);
                drawScaledCoinToBuffer(i, scale, current_coin_size);
                HAL_Sprite_PushImage(layout.x[i], layout.y[i], current_coin_size, current_coin_size, coin_buffer);
                screen_needs_push = true; 
                coins[i].needs_redraw = false;
            }
        }
        return screen_needs_push;
    }

    // 【函数说明】让指定硬币停止在目标正反面，设置闪烁帧并触发单枚停止回调。
    void stopCoin(int idx) {
        coins[idx].is_flipping = false;
        int heads_chance = 50 + sysConfig.coin_data.sanity;
        coins[idx].target_face = (random(100) < heads_chance) ? 0 : 1;
        coins[idx].current_angle = (coins[idx].target_face == 0) ? 0.0f : PI;
        coins[idx].needs_redraw = true;
        onCoinStop(idx);
    }

public:
    // 【函数说明】硬币动画主循环：推进角度、按自动计时停止硬币、绘制动态 UI，并在有变化时推屏。
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
                        // 【函数说明】让指定硬币停止在目标正反面，设置闪烁帧并触发单枚停止回调。
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
        bool overlay_pushed = false;
        if (ui_pushed || coin_pushed)
            overlay_pushed = drawOverlayUI();
        if (ui_pushed || coin_pushed || overlay_pushed) HAL_Screen_Update();

        if (global_is_animating && !any_active) {
            global_is_animating = false;
            onAllCoinsStopped();
        }
    }

   // 【函数说明】离开硬币动画页时停止动画标志，静态资源继续保留在全局资源缓存中。
   void onDestroy() override {
        sysAudio.stopWAV();
        // 退出时只释放橡皮擦缓存，不碰全局图片
        if (coin_buffer) { free(coin_buffer); coin_buffer = nullptr; }
    }
};

// ==========================================
// 派生 A：经典推演模式
// ==========================================
class AppCoinQuick : public AppCoinCore {
protected:
    // 【函数说明】返回硬币动画左侧保留区域宽度；快速和技能投掷都使用全宽动画，所以返回 0。
    int getLeftPanelWidth() override { return 0; } 
    int getTopPanelHeight() override { return 0; } 

    void drawStaticUI() override {
        drawOverlayUI();
    }

    // 【函数说明】快速模式的理智值固定压在右上角，并且每次硬币重绘后都会补画。
    bool drawOverlayUI() override {
        return drawSanityBadgeAt(REAL_SW - 8, 6);
    }

    // 【函数说明】单枚硬币停止时的回调，子类用它更新计数、点数和结果反馈。
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
        // 进入时申请硬币绘制缓冲区。缓冲区按最大运行显示尺寸申请，支持当前 UI 配置的最大舞台尺寸。
        if (!coin_buffer) coin_buffer = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
        if (!coin_buffer) {
            Serial.println("[硬币] 硬币绘制缓冲区申请失败，返回菜单。");
            appManager.popApp();
            return;
        }
        
        active_coins = sysConfig.coin_data.coin_count;
        if (active_coins < 1) active_coins = 1; if (active_coins > UICoin::MAX_COINS) active_coins = UICoin::MAX_COINS;
        global_is_animating = false;
        for(int i=0; i<active_coins; i++) { coins[i].current_angle = 0; coins[i].is_flipping = false; coins[i].flash_frames = 0; coins[i].needs_redraw = true; coins[i].target_face = 0; }
        HAL_Sprite_Clear(); drawActiveCoinsOnly(); drawStaticUI(); HAL_Screen_Update();
    }
    // 【函数说明】返回硬币页时重新执行 onCreate，重置一轮投掷动画。
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
    // 【函数说明】长按退出硬币投掷页并返回上一层菜单。
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
    // 【函数说明】返回硬币动画左侧保留区域宽度；快速和技能投掷都使用全宽动画，所以返回 0。
    int getLeftPanelWidth() override { return 0; } 

    /**
     * 技能投掷顶部状态栏高度。
     * 旧版本固定 26px，新屏字体放大后会挤压文字；这里跟随当前字体行高计算。
     */
    int getTopPanelHeight() override { return max(36, HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 14); } 

    /**
     * 绘制技能投掷顶部静态面板。
     * 左侧显示技能名，右侧显示当前累计点数，中间用半屏 splitX 分开。
     */
    void drawStaticUI() override {
        int top_h = getTopPanelHeight();
        int split_x = REAL_SW / 2;
        HAL_Draw_Line(0, top_h - 1, REAL_SW, top_h - 1, 0x18E3);
        eraseRect(0, 0, split_x, top_h - 2); 
        String name = sysConfig.coin_presets[g_coin_run_idx].name;
        int text_y = (top_h - HAL_Get_Font_Line_Height(HAL_FONT_BODY)) / 2;
        HAL_Screen_ShowChineseLine_Faded_Color(10, text_y, name.c_str(), 0.0f, TFT_CYAN);
        updateTopPanelScore(TFT_WHITE);
        drawOverlayUI();
    }

    // 【函数说明】技能预设模式也显示全局理智波动值，位置放在顶部面板右半区靠左侧。
    // 这样不会被下方硬币覆盖，也能和右侧当前点数同时显示。
    bool drawOverlayUI() override {
        int top_h = getTopPanelHeight();
        int y = max(2, (top_h - (HAL_Get_Font_Line_Height(HAL_FONT_BODY) + 6)) / 2);
        int x = REAL_SW / 2 + 64;
        return drawSanityBadgeAt(x, y);
    }

    /**
     * 绘制技能投掷顶部点数面板。
     *
     * 右侧常驻当前累计点数；当一枚硬币停止时，点数增量会从分数左侧向上漂浮。
     * 大屏下 splitX 取屏幕中线，不再使用旧屏写死的 140。
     */
    void updateTopPanelScore(uint16_t color) {
        int top_h = getTopPanelHeight();
        int split_x = REAL_SW / 2;
        eraseRect(split_x, 0, REAL_SW - split_x, top_h - 2); 
        char pow_str[16]; sprintf(pow_str, "%d", current_power); 
        int score_w = HAL_Get_Text_Width(pow_str);
        int score_x = REAL_SW - score_w - 12; 
        int score_y = (top_h - HAL_Get_Font_Line_Height(HAL_FONT_BODY)) / 2; 
        HAL_Screen_ShowChineseLine_Faded_Color(score_x, score_y, pow_str, 0.0f, color);

        if (top_pop_timer > 0) {
            int pop_w = HAL_Get_Text_Width(top_pop_text.c_str());
            int pop_x = score_x - pop_w - 10; 
            if (pop_x < split_x + 4) pop_x = split_x + 4;    
            int float_up = (20 - top_pop_timer) * max(8, top_h / 4) / 20; 
            int pop_y = score_y - float_up; 
            float fade_alpha = (20.0f - top_pop_timer) / 20.0f;
            HAL_Screen_ShowChineseLine_Faded_Color(pop_x, pop_y, top_pop_text.c_str(), fade_alpha, top_pop_color);
        }
    }

    // 【函数说明】绘制硬币页面中随结果变化的 UI，例如技能投掷的点数弹出；返回 true 表示需要推屏。
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

    // 【函数说明】单枚硬币停止时的回调，子类用它更新计数、点数和结果反馈。
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
    // 【函数说明】所有硬币停止后的回调，技能模式用它切换到结果阶段。
    void onAllCoinsStopped() override { phase = 2; }

public:
void onCreate() override {
        // 进入时申请硬币绘制缓冲区。缓冲区按最大运行显示尺寸申请，支持当前 UI 配置的最大舞台尺寸。
        if (!coin_buffer) coin_buffer = (uint16_t *)malloc(SRC_COIN_SIZE * SRC_COIN_SIZE * 2);
        if (!coin_buffer) {
            Serial.println("[硬币] 硬币绘制缓冲区申请失败，返回菜单。");
            appManager.popApp();
            return;
        }
        if (g_coin_run_idx < 0 || g_coin_run_idx >= sysConfig.coin_preset_count) {
            Serial.println("[硬币] 技能预设索引异常，返回硬币菜单。");
            appManager.popApp();
            return;
        }
        
        CoinPreset& p = sysConfig.coin_presets[g_coin_run_idx];
        active_coins = p.coin_count; current_power = p.base_power;
        if (active_coins < 1) active_coins = 1; if (active_coins > UICoin::MAX_COINS) active_coins = UICoin::MAX_COINS; 
        
        global_is_animating = false; phase = 0; top_pop_timer = 0;
        for(int i=0; i<active_coins; i++) { coins[i].current_angle = 0; coins[i].is_flipping = false; coins[i].flash_frames = 0; coins[i].needs_redraw = true; coins[i].target_face = 0; }
        HAL_Sprite_Clear(); drawActiveCoinsOnly(); drawStaticUI(); HAL_Screen_Update();
    }
    // 【函数说明】返回硬币页时重新执行 onCreate，重置一轮投掷动画。
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
    // 【函数说明】长按退出硬币投掷页并返回上一层菜单。
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

public:
    // 【函数说明】进入高级设置页时必须先处于普通浏览状态。
    // App 实例是静态复用的，如果上次编辑态残留，重新进入会看起来“一进来就被选中”。
    void onCreate() override
    {
        is_editing = false;
        AppMenuBase::onCreate();
    }

protected:
    // 【函数说明】返回硬币设置/菜单条目数量，决定 AppMenuBase 的循环滚动范围。
    int getMenuCount() override { return 4; } 

    const char *getTitle() override
    {
        return appManager.getLanguage() == LANG_ZH ? "决策参数设置" : "FLIP SETTINGS";
    }

    const char *getItemText(int index) override
    {
        static char text_buf[64];
        bool zh = appManager.getLanguage() == LANG_ZH;
        // 只有进入编辑状态后，当前被编辑的设置项左侧才显示“>”。
        // 平时保持普通菜单文本，避免高级设置页一直像“命令行光标”一样闪在左侧。
        const char *cursor = (is_editing && index == current_selection) ? "> " : "";

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

    // 【函数说明】编辑状态下把当前条目拆成“前缀 + 可跳动数值 + 后缀”。
    // 注意：这里仍然只在用户短按进入编辑后才返回 true；普通浏览状态不显示“>”，也不触发跳动动画。
    // AppMenuBase 会只对视觉中心项调用该接口，所以这里必须把左侧“>”写进 prefix，
    // 否则选中后的分段绘制会覆盖 getItemText() 里的完整文本，导致箭头消失。
    bool getItemEditParts(int index, const char **prefix, const char **anim_val, const char **suffix) override
    {
        if (!is_editing || index != current_selection)
            return false;

        static char val_str[16];
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (index == 0)
        {
            *prefix = zh ? "> 运行模式 [ " : "> MODE: [ ";
            *anim_val = sysConfig.coin_data.mode == 0 ? (zh ? "自动" : "AUTO") : (zh ? "手动" : "MANUAL");
            *suffix = " ]";
            return true;
        }
        else if (index == 1)
        {
            *prefix = zh ? "> 理智波动 [ " : "> SANITY: [ ";
            sprintf(val_str, "%+d", sysConfig.coin_data.sanity);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        else if (index == 2)
        {
            *prefix = zh ? "> 阵列数量 [ " : "> COINS: [ ";
            sprintf(val_str, "%d", sysConfig.coin_data.coin_count);
            *anim_val = val_str;
            *suffix = " ]";
            return true;
        }
        else if (index == 3)
        {
            *prefix = zh ? "> 硬币型号 [ " : "> TYPE: [ ";
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

    // 【函数说明】硬币高级设置页始终使用青色，避免编辑/选中状态反复换色。
    uint16_t getItemColor(int index) override
    {
        return 0x07FF;
    }

    // 【函数说明】硬币菜单/设置项确认入口，按 index 进入快速投掷、技能投掷、设置或预设编辑。
    void onItemClicked(int index) override
    {
        SYS_SOUND_CONFIRM();
        if (is_editing)
        {
            // 第二次短按表示确认当前设置值并退出编辑，条目恢复普通青色显示。
            is_editing = false;
            sysConfig.save();
        }
        else
        {
            // 第一次短按才进入编辑，左侧显示“>”。进入页面本身不默认选中任何项。
            is_editing = true;
        }
        AppMenuBase::onResume();
    }

    // 【函数说明】硬币投掷动画中旋钮不改变状态，避免用户中途改变结果。
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
                if (sysConfig.coin_data.coin_count > PrescriptConst::MAX_COIN_COUNT)
                    sysConfig.coin_data.coin_count = PrescriptConst::MAX_COIN_COUNT;
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

    // 【函数说明】硬币菜单长按返回上一级，设置页则保存配置后返回。
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
AppBase *appCoinSettings = &instanceCoinSettings;

// ==========================================
// 【终端本地操作】：量子技能录入与编辑终端
// ==========================================
class AppCoinPresetEdit : public AppBase {
private:
    int bp, cp, cc, cl, phase;
    DialAnimator dialAnim;       
    TacticalLinkEngine linkAnim; 

    // 【函数说明】绘制硬币预设编辑器：流程链路显示基础值、硬币值、数量、材质，中央滚轮显示当前数值。
    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 4) {
            char buf[64];
            if (g_coin_edit_idx >= 0) {
                sprintf(buf, zh ? "覆写 [%s]?" : "OVERRIDE [%s]?", sysConfig.coin_presets[g_coin_edit_idx].name.c_str());
            } else {
                sprintf(buf, zh ? "录入为新技能?" : "SAVE AS NEW?");
            }
            const char *tip = zh ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: CONFIRM";
            if (g_coin_edit_idx >= 0) tip = zh ? "长按抹除 / 单击确认" : "LONG: DELETE / CLICK: CONFIRM";
            UIFrame::DrawDangerConfirm(zh ? "战术覆写" : "OVERRIDE", buf, tip);
        } else {
            const char *names_zh[] = {"基础点", "硬币点", "抛掷数", "材质"};
            const char *names_en[] = {"BASE", "COIN", "COUNT", "MAT"};
            const char **names = zh ? names_zh : names_en;

            linkAnim.draw(UITheme::EditFlow::LinkY(), names, 4, phase, 58);

            UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

            if (phase == 0) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), bp, 0, 99, "");
            else if (phase == 1) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), cp, -20, 99, "");
            else if (phase == 2) dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), cc, 1, PrescriptConst::MAX_COIN_COUNT, "");
            else if (phase == 3) {
                const char *c_zh[] = {"经典金", "狂气红", "沉稳绿"};
                const char *c_en[] = {"GOLD", "RED", "GREEN"};
                dialAnim.drawStringDial(UITheme::EditFlow::DialY(), cl, zh ? c_zh : c_en, 3); 
            }

            const char *tip = zh ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
            if (phase == 0 && g_coin_edit_idx < 0) tip = zh ? "长按取消 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
            UIFrame::DrawTip(tip);
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

    // 【函数说明】返回硬币页时重新执行 onCreate，重置一轮投掷动画。
    void onResume() override { drawUI(); }

    void onLoop() override {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if (d_anim || l_anim) drawUI();
    }

    // 【函数说明】离开硬币动画页时停止动画标志，静态资源继续保留在全局资源缓存中。
    void onDestroy() override {}

    void onKnob(int delta) override {
        if (phase == 4) return;
        if (phase == 0) { bp += delta; if(bp < 0) bp = 99; if(bp > 99) bp = 0; }
        else if (phase == 1) { cp += delta; if(cp < -20) cp = 99; if(cp > 99) cp = -20; }
        else if (phase == 2) { cc += delta; if(cc < 1) cc = PrescriptConst::MAX_COIN_COUNT; if(cc > PrescriptConst::MAX_COIN_COUNT) cc = 1; }
        else if (phase == 3) { cl += delta; if(cl < 0) cl = 2; if(cl > 2) cl = 0; }
        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    // 【函数说明】短按在未动画时重新开始一轮投掷；动画中通常用于跳过或不处理。
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
                if (sysConfig.coin_preset_count >= PrescriptConst::MAX_COIN_PRESETS) {
                    Serial.println("[硬币] 预设数量已满，无法继续录入。");
                    SYS_SOUND_NAV();
                    appManager.popApp();
                    return;
                }
                int idx = sysConfig.coin_preset_count;
                sysConfig.coin_presets[idx].base_power = bp;
                sysConfig.coin_presets[idx].coin_power = cp;
                sysConfig.coin_presets[idx].coin_count = cc;
                String c_str = ""; for(int i=0; i<cc; i++) c_str += String(cl);
                sysConfig.coin_presets[idx].coin_colors = c_str;
                char autoName[16];
                sprintf(autoName, appManager.getLanguage() == LANG_ZH ? "预设-%d" : "PRESET-%d", idx + 1);
                sysConfig.coin_presets[idx].name = autoName;
                sysConfig.coin_preset_count++;
            }
            sysConfig.save();
            appManager.popApp();
        }
    }

    // 【函数说明】长按退出硬币投掷页并返回上一层菜单。
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
    // 【函数说明】返回硬币设置/菜单条目数量，决定 AppMenuBase 的循环滚动范围。
    int getMenuCount() override { 
        return sysConfig.coin_preset_count + 3; 
    }
    
    const char *getTitle() override { 
        return appManager.getLanguage() == LANG_ZH ? "量子决策模块" : "QUANTUM FLIP"; 
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

   // 【函数说明】硬币菜单/设置项确认入口，按 index 进入快速投掷、技能投掷、设置或预设编辑。
   void onItemClicked(int index) override
    {
        if (index == 0) {
            g_coin_run_idx = -1;
            appManager.push(AppId::CoinQuick); 
        } 
        else if (index == sysConfig.coin_preset_count + 1) {
            if (sysConfig.coin_preset_count < PrescriptConst::MAX_COIN_PRESETS) {
                g_coin_edit_idx = -1; 
                appManager.push(AppId::CoinPresetEdit);
            }
        } 
        else if (index == sysConfig.coin_preset_count + 2) {
            appManager.push(AppId::CoinSettings);
        } 
        else {
            int preset_idx = index - 1;
            if (preset_idx < 0 || preset_idx >= sysConfig.coin_preset_count) {
                SYS_SOUND_NAV();
                return;
            }
            g_coin_run_idx = preset_idx; 
            appManager.push(AppId::CoinSkill); 
        }
    }

    // 【函数说明】硬币菜单长按返回上一级，设置页则保存配置后返回。
    void onLongPressed() override
    {
        if (current_selection > 0 && current_selection <= sysConfig.coin_preset_count) {
            g_coin_edit_idx = current_selection - 1;
            appManager.push(AppId::CoinPresetEdit); 
        } else {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

AppCoinMenu instanceCoinMenu;
AppBase *appCoinFlip = &instanceCoinMenu;
