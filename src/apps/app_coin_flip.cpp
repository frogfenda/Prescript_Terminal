// 文件：src/apps/app_coin_flip.cpp
#include "app_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>

#define COIN_SIZE 64  

// ==========================================
// 【核心接口定义】
// ==========================================

// 1. 运行模式枚举
enum CoinFlipMode {
    COIN_MODE_AUTO,   // 模式 1：按下狂转，到达随机圈数后自动阻尼刹车
    COIN_MODE_MANUAL  // 模式 2：按下无尽旋转，再次按下后才开始阻尼刹车
};

// 2. 硬币配置接口结构体
struct CoinConfig {
    const char* title_zh;       // 中文标题
    const char* title_en;       // 英文标题
    const char* path_heads;     // 正面图像路径
    const char* path_tails;     // 反面图像路径
    const char* res_heads_zh;   // 正面中文文案
    const char* res_heads_en;   // 正面英文文案
    const char* res_tails_zh;   // 反面中文文案
    const char* res_tails_en;   // 反面英文文案
    CoinFlipMode mode;          // 旋转模式
};

// ==========================================
// 【通用硬币物理引擎类】
// ==========================================
class AppCoinFlip : public AppBase {
private:
    CoinConfig config; // 当前实例的配置参数

    uint16_t* img_heads = nullptr;
    uint16_t* img_tails = nullptr;
    uint16_t* coin_buffer = nullptr; 
    
    float current_angle = 0;
    float target_angle = 0;
    
    // 物理引擎状态机
    bool is_flipping = false;
    bool is_stopping = false; // 专为手动模式设计的“刹车阶段”标志

    int last_half_turn = 0;
    uint32_t last_frame_time = 0;
    bool ui_needs_full_redraw = true; 

    void loadCoinData() {
        if (!img_heads) img_heads = (uint16_t*)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!img_tails) img_tails = (uint16_t*)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!coin_buffer) coin_buffer = (uint16_t*)malloc(COIN_SIZE * COIN_SIZE * 2);

        // 动态读取配置接口里的路径
        File f1 = LittleFS.open(config.path_heads, "r");
        if (f1) { f1.read((uint8_t*)img_heads, COIN_SIZE * COIN_SIZE * 2); f1.close(); }
        
        File f2 = LittleFS.open(config.path_tails, "r");
        if (f2) { f2.read((uint8_t*)img_tails, COIN_SIZE * COIN_SIZE * 2); f2.close(); }
    }

    void drawScaledCoinToBuffer(float scaleX) {
        memset(coin_buffer, 0, COIN_SIZE * COIN_SIZE * 2);

        uint16_t* img = (scaleX >= 0) ? img_heads : img_tails;
        if (!img) return; 

        int drawW = COIN_SIZE * fabs(scaleX);
        if (drawW % 2 != 0) drawW++; 
        if (drawW <= 0) return; 

        int startX = (COIN_SIZE - drawW) / 2;

        if (drawW <= 4) {
            for (int y = 0; y < COIN_SIZE; y++) {
                for (int x = 0; x < drawW; x++) {
                    coin_buffer[y * COIN_SIZE + startX + x] = 0x03E0; 
                }
            }
            return;
        }

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
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();
        bool zh = appManager.getLanguage() == LANG_ZH;

        int target_x = 50 - 32;
        int target_y = sh / 2 - 32;

        if (ui_needs_full_redraw) {
            HAL_Sprite_Clear(); 
            int text_x = 100;
            
            // 动态读取标题
            HAL_Screen_ShowChineseLine(text_x, 8, zh ? config.title_zh : config.title_en);
            
            const char* tip = "";
            if (is_flipping) {
                if (config.mode == COIN_MODE_MANUAL && !is_stopping) {
                    tip = zh ? ">> 单击: 锁定决断" : ">> CLICK: LOCK";
                } else {
                    tip = zh ? "[ 推演中... ]" : "[ CALCULATING... ]";
                }
            } else if (current_angle == 0) {
                tip = zh ? "单击: 抛掷 / 长按: 退出" : "CLICK: FLIP / LONG: EXIT";
            } else {
                tip = zh ? "[ 决断已定 ]" : "[ DECISION MADE ]";
            }
            HAL_Screen_ShowChineseLine_Faded(text_x, 32, tip, 0.4f);

            if (!is_flipping && current_angle > 0) {
                int current_half_turn = round(current_angle / PI);
                bool is_heads = (current_half_turn % 2 == 0);
                
                // 动态读取落地结果文案
                const char* res = zh ? (is_heads ? config.res_heads_zh : config.res_tails_zh) 
                                     : (is_heads ? config.res_heads_en : config.res_tails_en);
                HAL_Screen_ShowChineseLine_Faded_Color(text_x, 56, res, 0.0f, 0x07FF); 
            }
            ui_needs_full_redraw = false; 
        }

        float scale = cos(current_angle);
        drawScaledCoinToBuffer(scale);
        HAL_Sprite_PushImage(target_x, target_y, 64, 64, coin_buffer);
        HAL_Screen_Update(); 
    }

public:
    // 构造函数：接收接口配置参数
    AppCoinFlip(CoinConfig cfg) : config(cfg) {}

    void onCreate() override {
        loadCoinData();
        current_angle = 0;
        target_angle = 0;
        is_flipping = false;
        is_stopping = false;
        ui_needs_full_redraw = true;
        drawUI();
    }
    
    void onResume() override { 
        ui_needs_full_redraw = true;
        drawUI(); 
    }

    void onLoop() override {
        if (is_flipping) {
            uint32_t now = millis();
            if (now - last_frame_time < 16) return; 
            last_frame_time = now;

            // ==========================================
            // 【物理引擎状态机：无缝衔接无尽与刹车】
            // ==========================================
            float step = 0;
            
            if (config.mode == COIN_MODE_MANUAL && !is_stopping) {
                // 无尽模式：全速旋转
                step = 0.28f; 
            } else {
                // 阻尼刹车阶段（自动模式，或手动模式按下停止后触发）
                float dist = target_angle - current_angle;
                step = dist * 0.06f; 
                
                if (step > 0.3f) step = 0.3f; 
                if (step < 0.03f) step = 0.03f; 
            }

            current_angle += step;

            // 落地判定 (只在刹车阶段检测)
            if ((config.mode == COIN_MODE_AUTO || is_stopping) && current_angle >= target_angle) {
                current_angle = target_angle;
                is_flipping = false;
                is_stopping = false;
                ui_needs_full_redraw = true; 
                SYS_SOUND_CONFIRM(); 
            }

            int current_half_turn = (int)(current_angle / PI);
            if (current_half_turn != last_half_turn) {
                SYS_SOUND_NAV(); 
                last_half_turn = current_half_turn;
            }

            drawUI();
        }
    }

    void onDestroy() override {
        if (img_heads) { free(img_heads); img_heads = nullptr; }
        if (img_tails) { free(img_tails); img_tails = nullptr; }
        if (coin_buffer) { free(coin_buffer); coin_buffer = nullptr; }
    }

    void onKnob(int delta) override { }

    void onKeyShort() override {
        if (is_flipping) {
            // 【手动模式拦截】：如果正在无尽旋转，单击拦截并开始平滑刹车
            if (config.mode == COIN_MODE_MANUAL && !is_stopping) {
                SYS_SOUND_CONFIRM();
                is_stopping = true; // 切换状态为阻尼刹车
                ui_needs_full_redraw = true;
                
                // 瞬间计算刹车落点：再转 3~5 圈后优雅地停下
                int target_face = random(2); 
                int spins = random(6, 10); 
                int current_n = round(current_angle / PI);
                int final_n = current_n + spins;
                
                if (target_face == 0 && (final_n % 2 != 0)) final_n++; 
                if (target_face == 1 && (final_n % 2 == 0)) final_n++; 
                
                target_angle = final_n * PI;
            }
            return; 
        }

        // --- 起步阶段 ---
        SYS_SOUND_CONFIRM();
        is_flipping = true;
        is_stopping = false;
        ui_needs_full_redraw = true; 
        last_half_turn = (int)(current_angle / PI);

        if (config.mode == COIN_MODE_AUTO) {
            // 自动模式起步就定好落点
            int target_face = random(2); 
            int spins = random(8, 14); 
            int current_n = round(current_angle / PI);
            int final_n = current_n + spins;
            
            if (target_face == 0 && (final_n % 2 != 0)) final_n++; 
            if (target_face == 1 && (final_n % 2 == 0)) final_n++; 
            
            target_angle = final_n * PI;
        } 
        // 手动模式起步不需要定落点，无限转就完了

        last_frame_time = millis();
    }

    void onKeyLong() override {
        if (!is_flipping) {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

// ==========================================
// 【接口使用演示：实例化各种硬币】
// ==========================================

// 1. 实例化你原本的：量子决策模块 (自动模式)
CoinConfig configQuantum = {
    ">> 量子决策模块", ">> QUANTUM FLIP",
    "/assets/coins/heads.bin", "/assets/coins/tails.bin",
    "结果: [ 正面 ]", "RES: [ HEADS ]",
    "结果: [ 反面 ]", "RES: [ TAILS ]",
    COIN_MODE_AUTO   // <--- 自动停
};
AppCoinFlip instanceCoinFlip(configQuantum);
AppBase* appCoinFlip = &instanceCoinFlip;

