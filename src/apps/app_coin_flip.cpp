// 文件：src/apps/app_coin_flip.cpp
#include "app_base.h"
#include "app_manager.h"
#include "hal/hal.h"
#include <LittleFS.h>

// 【重要】专为 76 像素高屏幕定制的尺寸！请传入 64x64 的 .bin 素材
#define COIN_SIZE 64  

class AppCoinFlip : public AppBase {
private:
    uint16_t* img_heads = nullptr;
    uint16_t* img_tails = nullptr;
    
    float current_angle = 0;
    float target_angle = 0;
    bool is_flipping = false;
    int last_half_turn = 0;

    void loadCoinData() {
        if (!img_heads) img_heads = (uint16_t*)malloc(COIN_SIZE * COIN_SIZE * 2);
        if (!img_tails) img_tails = (uint16_t*)malloc(COIN_SIZE * COIN_SIZE * 2);

        File f1 = LittleFS.open("/assets/coins/heads.bin", "r");
        if (f1) { f1.read((uint8_t*)img_heads, COIN_SIZE * COIN_SIZE * 2); f1.close(); }
        
        File f2 = LittleFS.open("/assets/coins/tails.bin", "r");
        if (f2) { f2.read((uint8_t*)img_tails, COIN_SIZE * COIN_SIZE * 2); f2.close(); }
    }

    void drawScaledCoin(int cx, int cy, float scaleX) {
        uint16_t* img = (scaleX >= 0) ? img_heads : img_tails;
        if (!img) return; 

        int drawW = COIN_SIZE * fabs(scaleX);
        int drawH = COIN_SIZE;
        int startX = cx - drawW / 2;
        int startY = cy - drawH / 2;

        if (drawW <= 0) return; 

        for (int y = 0; y < drawH; y++) {
            for (int x = 0; x < drawW; x++) {
                int origX = x * COIN_SIZE / drawW;
                uint16_t color = img[y * COIN_SIZE + origX];
                
                // 剔除纯黑背景 (假设硬币背景是纯黑 0x0000)，实现透明抠图效果！
                if (color != 0x0000) {
                    HAL_Draw_Pixel(startX + x, startY + y, color); 
                }
            }
        }
    }

    void drawUI() {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();  // 284
        int sh = HAL_Get_Screen_Height(); // 76
        bool zh = appManager.getLanguage() == LANG_ZH;

        // 左侧画硬币，中心点定在 X=50, Y=38
        float scale = cos(current_angle);
        drawScaledCoin(50, sh / 2, scale);

        // 右侧画文字信息 (为硬币腾出左边的空间)
        int text_x = 100;
        HAL_Screen_ShowChineseLine(text_x, 8, zh ? ">> 量子决策模块" : ">> QUANTUM FLIP");

        const char* tip = "";
        if (!is_flipping) {
            tip = zh ? "单击: 抛掷 / 长按: 退出" : "CLICK: FLIP / LONG: EXIT";
        } else {
            tip = zh ? "[ 推演中... ]" : "[ CALCULATING... ]";
        }
        HAL_Screen_ShowChineseLine_Faded(text_x, 32, tip, 0.4f);

        // 如果停下来了，在最下面显示最终结果文字
        if (!is_flipping && current_angle > 0) {
            int current_half_turn = round(current_angle / PI);
            bool is_heads = (current_half_turn % 2 == 0);
            const char* res = zh ? (is_heads ? "结果: [ 正面 ]" : "结果: [ 反面 ]") 
                                 : (is_heads ? "RES: [ HEADS ]" : "RES: [ TAILS ]");
            HAL_Screen_ShowChineseLine_Faded_Color(text_x, 56, res, 0.0f, 0x07FF); // 霓虹青高亮
        }

        HAL_Screen_Update();
    }

public:
    void onCreate() override {
        loadCoinData();
        current_angle = 0;
        target_angle = 0;
        is_flipping = false;
        drawUI();
    }

    void onResume() override { drawUI(); }

    void onLoop() override {
        if (is_flipping) {
            current_angle += (target_angle - current_angle) * 0.08;

            int current_half_turn = (int)(current_angle / PI);
            if (current_half_turn != last_half_turn) {
                SYS_SOUND_NAV(); 
                last_half_turn = current_half_turn;
            }

            if (fabs(target_angle - current_angle) < 0.05) {
                current_angle = target_angle;
                is_flipping = false;
                SYS_SOUND_CONFIRM(); 
            }
            drawUI();
        }
    }

    void onDestroy() override {
        if (img_heads) { free(img_heads); img_heads = nullptr; }
        if (img_tails) { free(img_tails); img_tails = nullptr; }
    }

    void onKnob(int delta) override { }

    void onKeyShort() override {
        if (is_flipping) return; 

        SYS_SOUND_CONFIRM();
        is_flipping = true;
        last_half_turn = (int)(current_angle / PI);

        int target_face = random(2); 
        int spins = random(10, 20);
        
        int current_n = round(current_angle / PI);
        int final_n = current_n + spins;
        
        if (target_face == 0 && (final_n % 2 != 0)) spins++; 
        if (target_face == 1 && (final_n % 2 == 0)) spins++; 

        target_angle = current_n * PI + spins * PI;
    }

    void onKeyLong() override {
        if (!is_flipping) {
            SYS_SOUND_NAV();
            appManager.popApp();
        }
    }
};

AppCoinFlip instanceCoinFlip;
AppBase* appCoinFlip = &instanceCoinFlip;